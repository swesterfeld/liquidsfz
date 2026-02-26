// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <random>
#include <algorithm>
#include <mutex>
#include <assert.h>

#include "loader.hh"
#include "utils.hh"
#include "envelope.hh"
#include "voice.hh"
#include "liquidsfz.hh"
#include "pugixml.hh"

namespace LiquidSFZInternal
{

using LiquidSFZ::Log;

struct Channel
{
  std::vector<uint8_t> cc_values = std::vector<uint8_t> (128, 0);
  int pitch_bend = 0x2000;

  void
  init (const Control& control)
  {
    std::fill (cc_values.begin(), cc_values.end(), 0);
    for (auto set_cc : control.set_cc)
      if (set_cc.cc >= 0 && set_cc.cc <= 127)
        cc_values[set_cc.cc] = std::clamp (set_cc.value, 0, 127);
    pitch_bend = 0x2000;
  }
};

class Global
{
  static std::mutex            mutex_;
  static std::weak_ptr<Global> global_;

public:
  SampleCache sample_cache;

  static std::shared_ptr<Global>
  get()
  {
    std::lock_guard lg (mutex_);

    auto old_instance = global_.lock();
    if (old_instance)
      {
        return old_instance;
      }
    else
      {
        auto new_instance = std::make_shared<Global>();
        global_ = new_instance;
        return new_instance;
      }
  }
};

struct ProgramInfo
{
  int         index = 0;
  std::string name;
  std::string sfz_filename;
};

class Synth
{
public:
  static constexpr uint MAX_BLOCK_SIZE = 1024;

private:
  std::shared_ptr<Global> global_;
  std::minstd_rand random_gen;
  std::function<void (Log, const char *)> log_function_;
  std::function<void (double)> progress_function_;
  uint sample_rate_ = 44100; // default
  uint64_t global_frame_count = 0;
  std::vector<Voice>   voices_;
  std::vector<Voice *> active_voices_;
  std::vector<Voice *> idle_voices_;
  bool                 idle_voices_changed_ = false;
  std::vector<Region>  regions_;
  Control control_;
  std::vector<ProgramInfo> bank_programs_;
  std::vector<Control::Define> bank_defines_;
  std::vector<CCInfo> cc_list_;
  std::vector<KeyInfo> key_list_;
  CurveTable curve_table_;
  std::vector<Curve> curves_;
  Limits limits_;
  Log log_level_ = Log::INFO;
  float gain_ = 1.0;
  bool  live_mode_ = true;
  int   sample_quality_ = 3;
  uint  preload_time_ = 500;
  std::array<bool, 128> is_key_switch_;
  std::array<bool, 128> is_supported_cc_;

  static constexpr int CC_SUSTAIN       = 64;
  static constexpr int CC_ALL_SOUND_OFF = 120;
  static constexpr int CC_ALL_NOTES_OFF = 123;

  std::array<float, MAX_BLOCK_SIZE> const_block_0_, const_block_1_;

  void
  init_channels()
  {
    for (auto& channel : channels_)
      channel.init (control_);
  }
  void sort_events_stable();
public:
  Synth() :
    global_ (Global::get()) // init data shared between all Synth instances
  {
    std::random_device random_dev;
    random_gen.seed (random_dev());

    // preallocate event buffer to avoid malloc in audio thread
    events.reserve (1024);

    // constant blocks
    const_block_0_.fill (0.f);
    const_block_1_.fill (1.f);

    // sane defaults:
    set_max_voices (256);
    set_channels (16);
  }
  ~Synth()
  {
    all_sound_off();
  }
  void
  set_sample_rate (uint sample_rate)
  {
    sample_rate_ = sample_rate;
  }
  uint
  sample_rate()
  {
    return sample_rate_;
  }
  void
  set_max_voices (uint n_voices)
  {
    voices_.clear();
    active_voices_.clear();
    idle_voices_.clear();
    idle_voices_changed_ = false;

    for (uint i = 0; i < n_voices; i++)
      voices_.emplace_back (this, limits_);

    for (auto& voice : voices_)
      idle_voices_.push_back (&voice);

    active_voices_.reserve (n_voices);
  }
  uint
  max_voices()
  {
    return voices_.size();
  }
  void
  set_live_mode (bool live_mode)
  {
    live_mode_ = live_mode;
  }
  bool
  live_mode() const
  {
    return live_mode_;
  }
  void
  set_preload_time (uint time_ms)
  {
    preload_time_ = time_ms;
  }
  uint
  preload_time()
  {
    return preload_time_;
  }
  void
  set_sample_quality (int sample_quality)
  {
    sample_quality_ = std::clamp (sample_quality, 1, 3);
  }
  int
  sample_quality()
  {
    return sample_quality_;
  }
  void
  set_channels (uint n_channels)
  {
    channels_.resize (n_channels);
    init_channels();
  }
  void
  set_gain (float gain)
  {
    gain_ = gain;

    for (Voice *voice : active_voices_)
      voice->update_gain();
  }
  float
  gain() const
  {
    return gain_;
  }
  bool
  load (const std::string& filename)
  {
    /* plain SFZ load -> no programs, no defines */
    bank_defines_.clear();
    bank_programs_.clear();

    return load_internal (filename);
  }
  bool
  load_internal (const std::string& filename)
  {
    Loader loader (this);
    if (loader.parse (filename, global_->sample_cache, bank_defines_))
      {
        regions_      = loader.regions;
        control_      = loader.control;
        cc_list_      = loader.cc_list;
        key_list_     = loader.key_list;
        limits_       = loader.limits;
        curve_table_  = std::move (loader.curve_table);
        curves_       = std::move (loader.curves);

        // old sample data can only be freed after assigning regions_
        global_->sample_cache.cleanup_post_load();

        is_key_switch_.fill (false);
        for (auto k : key_list_)
          if (k.is_switch && k.key >= 0 && uint (k.key) < is_key_switch_.size())
            is_key_switch_[k.key] = true;

        is_supported_cc_.fill (false);
        for (auto c : cc_list_)
          if (c.cc >= 0 && uint (c.cc) < is_supported_cc_.size())
            is_supported_cc_[c.cc] = true;

        // we must reinit all voices
        //  - ensure that there are no pointers to old regions (which are deleted)
        //  - adjust voice state to the new global limits for this sfz
        set_max_voices (voices_.size());

        init_channels();
        return true;
      }
    return false;
  }
  bool
  is_bank (const std::string& filename)
  {
    pugi::xml_document doc;
    auto result = doc.load_file (filename.c_str());
    if (!result)
      return false;

    auto bank = doc.child ("AriaBank");
    if (!bank)
      return false;

    auto program = bank.child ("AriaProgram");
    if (!program)
      return false;

    return true;
  }
  bool
  load_bank (const std::string& filename)
  {
    pugi::xml_document doc;
    auto result = doc.load_file (filename.c_str());
    if (!result)
      return false;

    auto bank = doc.child ("AriaBank");
    if (!bank)
      return false;

    std::vector<ProgramInfo> programs;
    int i = 0;
    for (pugi::xml_node program : bank.children ("AriaProgram"))
      {
        std::string name = program.attribute("name").as_string();

        auto elem = program.child ("AriaElement");
        std::string p = elem.attribute ("path").as_string();

        programs.push_back (ProgramInfo { i++, name, path_resolve_case_insensitive (path_absolute (path_join (path_dirname (filename), p))) });
      }
    std::vector<Control::Define> defines;
    for (pugi::xml_node define : bank.children ("Define"))
      {
        std::string name = define.attribute ("name").as_string();
        std::string value = define.attribute ("value").as_string();

        Control::Define def;
        def.variable = name;
        def.value = value;
        defines.push_back (def);
      }
    if (programs.size())
      {
        bank_programs_ = programs;
        bank_defines_ = defines;
        return true;
      }

    return false;
  }
  bool
  select_program (uint program)
  {
    return load_internal (bank_programs_[program].sfz_filename);
  }
  std::vector<ProgramInfo>
  list_programs()
  {
    return bank_programs_;
  }
  std::vector<CCInfo>
  list_ccs()
  {
    return cc_list_;
  }
  std::vector<KeyInfo>
  list_keys()
  {
    return key_list_;
  }
  void
  progress (double percent)
  {
    if (progress_function_)
      progress_function_ (percent);
  }
  Voice *
  alloc_voice()
  {
    if (!idle_voices_.empty())
      {
        Voice *voice = idle_voices_.back();
        idle_voices_.pop_back();
        active_voices_.push_back (voice);

        return voice;
      }
    debug ("alloc_voice: no voices left\n");
    return nullptr;
  }
  void
  idle_voices_changed()
  {
    idle_voices_changed_ = true;
  }
  void
  update_idle_voices()
  {
    if (idle_voices_changed_)
      {
        size_t new_voice_count = 0;

        for (size_t i = 0; i < active_voices_.size(); i++)
          {
            Voice *voice = active_voices_[i];

            if (voice->state_ == Voice::IDLE)    // voice used?
              {
                idle_voices_.push_back (voice);
              }
           else
             {
               active_voices_[new_voice_count++] = voice;
             }
          }
        active_voices_.resize (new_voice_count);

        idle_voices_changed_ = false;
      }
  }
  uint
  active_voice_count()
  {
    return active_voices_.size();
  }
  size_t
  cache_size()
  {
    return global_->sample_cache.cache_size();
  }
  uint
  cache_file_count()
  {
    return global_->sample_cache.cache_file_count();
  }
  void
  set_max_cache_size (size_t max_cache_size)
  {
    global_->sample_cache.set_max_cache_size (max_cache_size);
  }
  size_t
  max_cache_size()
  {
    return global_->sample_cache.max_cache_size();
  }
  void
  note_on (int chan, int key, int vel)
  {
    /* kill overlapping notes */
    for (Voice *voice : active_voices_)
      {
        if (voice->state_ == Voice::ACTIVE &&
            voice->trigger_ == Trigger::ATTACK &&
            voice->channel_ == chan && voice->key_ == key && voice->region_->loop_mode != LoopMode::ONE_SHOT)
          {
            release (*voice); // FIXME: we may want to use a fast release here
          }
      }
    trigger_regions (Trigger::ATTACK, chan, key, vel, /* time_since_note_on */ 0.0);
  }

  double
  normalized_random_value()
  {
    // - random must be >= 0.0
    // - random must be <  1.0  (and never 1.0)
    return random_gen() / double (random_gen.max() + 1);
  }
  void
  trigger_regions (Trigger trigger, int chan, int key, int vel, double time_since_note_on)
  {
    // - random must be >= 0.0
    // - random must be <  1.0  (and never 1.0)
    double random = normalized_random_value();

    for (auto& region : regions_)
      {
        if (is_key_switch_[key] && region.sw_lokey <= key && region.sw_hikey >= key && trigger == Trigger::ATTACK)
          region.switch_match = region.sw_lolast <= key && region.sw_hilast >= key;

        if (region.lokey <= key && region.hikey >= key &&
            region.lovel <= vel && region.hivel >= vel &&
            region.trigger == trigger)
          {
            bool cc_match = true;
            for (size_t cc = 0; cc < region.locc.size(); cc++)
              {
                if (region.locc[cc] != 0 || region.hicc[cc] != 127)
                  {
                    const int val = get_cc (chan, cc);
                    if (val < region.locc[cc] || val > region.hicc[cc])
                      cc_match = false;
                  }
              }
            if (!cc_match)
              continue;
            if (!region.switch_match)
              continue;

            if (region.play_seq == region.seq_position)
              {
                /* in order to make sequences and random play nice together
                 * random check needs to be done inside sequence check
                 */
                if (region.lorand <= random && region.hirand > random)
                  {
                    if (region.cached_sample)
                      {
                        /* handle off_by */
                        if (region.group)
                          {
                            for (Voice *voice : active_voices_)
                              {
                                if (voice->state_ == Voice::ACTIVE)
                                  {
                                    /* off_by should not affect voices started by this trigger_regions call */
                                    const bool voice_is_new = (voice->start_frame_count_ == global_frame_count);

                                    if (voice->off_by() == region.group && !voice_is_new)
                                      {
                                        voice->stop (voice->region_->off_mode);
                                      }
                                  }
                              }
                          }

                        /* start new voice */
                        auto voice = alloc_voice();
                        if (voice)
                          voice->start (region, chan, key, vel, time_since_note_on, global_frame_count, sample_rate_);
                      }
                  }
              }
            region.play_seq++;
            if (region.play_seq > region.seq_length)
              region.play_seq = 1;
          }
      }
    // log_debug ("### active voice count: %d\n", active_voice_count());
  }
  void
  note_off (int chan, int key)
  {
    const bool sustain_pedal = get_cc (chan, CC_SUSTAIN) >= 0x40;

    for (Voice *voice : active_voices_)
      {
        if (voice->state_ == Voice::ACTIVE &&
            voice->trigger_ == Trigger::ATTACK &&
            voice->channel_ == chan && voice->key_ == key && voice->region_->loop_mode != LoopMode::ONE_SHOT)
          {
            if (sustain_pedal)
              {
                voice->state_ = Voice::SUSTAIN;
              }
            else
              {
                release (*voice);
              }
          }
      }
  }
  void
  release (Voice& voice)
  {
    if (voice.state_ != Voice::ACTIVE && voice.state_ != Voice::SUSTAIN)
      {
        debug ("release: state %d not active/sustain\n", voice.state_);
        return;
      }
    voice.stop (OffMode::NORMAL);

    double time_since_note_on = (global_frame_count - voice.start_frame_count_) / double (sample_rate_);
    trigger_regions (Trigger::RELEASE, voice.channel_, voice.key_, voice.velocity_, time_since_note_on);
  }
  std::vector<Channel> channels_;
  void
  update_cc (int channel, int controller, int value)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("update_cc: bad channel %d\n", channel);
        return;
      }
    auto& ch = channels_[channel];
    if (controller < 0 || uint (controller) >= ch.cc_values.size())
      {
        debug ("update_cc: bad channel controller %d\n", controller);
        return;
      }
    if (!is_supported_cc_[controller] && (controller == CC_ALL_SOUND_OFF || controller == CC_ALL_NOTES_OFF))
      {
        all_sound_off();
        return;
      }
    ch.cc_values[controller] = value;

    for (Voice *voice : active_voices_)
      {
        if (voice->channel_ == channel)
          voice->update_cc (controller);
      }
    if (controller == CC_SUSTAIN)
      {
        if (value < 0x40)
          {
            for (Voice *voice : active_voices_)
              {
                if (voice->state_ == Voice::SUSTAIN)
                  release (*voice);
              }
          }
      }
  }
  int
  get_cc (int channel, int controller) const
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("get_cc: bad channel %d\n", channel);
        return 0;
      }
    auto& ch = channels_[channel];
    if (controller < 0 || uint (controller) >= ch.cc_values.size())
      {
        debug ("get_cc: bad channel controller %d\n", controller);
        return 0;
      }
    return ch.cc_values[controller];
  }
  float
  get_curve_value (int curve, int value) const
  {
    if (curve >= 0 && curve < int (curves_.size()))
      {
        if (!curves_[curve].empty())
          return curves_[curve].get (value);
      }
    return 0;
  }
  float
  get_cc_curve (int channel, const CCParamVec::Entry& entry) const
  {
    int curvecc = entry.curvecc;
    if (curvecc >= 0 && curvecc < int (curves_.size()))
      {
        if (!curves_[curvecc].empty())
          return curves_[entry.curvecc].get (get_cc (channel, entry.cc));
      }
    return get_cc (channel, entry.cc) * (1 / 127.f);
  }
  float
  get_cc_vec_value (int channel, const CCParamVec& cc_param_vec) const
  {
    float value = 0.0;
    for (const auto& entry : cc_param_vec)
      value += get_cc_curve (channel, entry) * entry.value;
    return value;
  }
  void
  update_pitch_bend (int channel, int value)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("update_pitch_bend: bad channel %d\n", channel);
        return;
      }
    auto& ch = channels_[channel];
    ch.pitch_bend = value;

    for (Voice *voice : active_voices_)
      {
        if (voice->channel_ == channel)
          voice->update_pitch_bend (value);
      }
  }
  int
  get_pitch_bend (int channel)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("get_pitch_bend: bad channel %d\n", channel);
        return 0;
      }
    return channels_[channel].pitch_bend;
  }
  const float *
  const_block_0() const
  {
    return const_block_0_.data();
  }
  const float *
  const_block_1() const
  {
    return const_block_1_.data();
  }
  struct Event
  {
    enum class Type : uint16_t { NONE, NOTE_ON, NOTE_OFF, CC, PITCH_BEND };

    uint     time_frames = 0;
    uint     tmp_sort_index = 0;
    Type     type = Type::NONE;
    uint16_t channel = 0;
    uint16_t arg1 = 0;
    uint16_t arg2 = 0;
  };
  std::vector<Event> events;
  void
  add_event_note_on (uint time_frames, int channel, int key, int velocity)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("add_event_note_on: bad channel %d\n", channel);
        return;
      }
    if (key < 0 || key > 127)
      {
        debug ("add_event_note_on: bad key %d\n", key);
        return;
      }
    if (velocity < 0 || velocity > 127)
      {
        debug ("add_event_note_on: bad velocity %d\n", velocity);
        return;
      }
    if (velocity == 0)
      {
        add_event_note_off (time_frames, channel, key);
        return;
      }
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::NOTE_ON;
    event.channel = channel;
    event.arg1 = key;
    event.arg2 = velocity;
    push_event_no_malloc (event);
  }
  void
  add_event_note_off (uint time_frames, int channel, int key)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("add_event_note_off: bad channel %d\n", channel);
        return;
      }
    if (key < 0 || key > 127)
      {
        debug ("add_event_note_off: bad key %d\n", key);
        return;
      }
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::NOTE_OFF;
    event.channel = channel;
    event.arg1 = key;
    event.arg2 = 0;
    push_event_no_malloc (event);
  }
  void
  add_event_cc (uint time_frames, int channel, int cc, int value)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("add_event_cc: bad channel %d\n", channel);
        return;
      }
    if (cc < 0 || cc > 127)
      {
        debug ("add_event_cc: bad cc %d\n", cc);
        return;
      }
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::CC;
    event.channel = channel;
    event.arg1 = cc;
    event.arg2 = std::clamp (value, 0, 127);
    push_event_no_malloc (event);
  }
  void
  add_event_pitch_bend (uint time_frames, int channel, int value)
  {
    if (channel < 0 || uint (channel) >= channels_.size())
      {
        debug ("add_event_pitch_bend: bad channel %d\n", channel);
        return;
      }
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::PITCH_BEND;
    event.channel = channel;
    event.arg1 = std::clamp (value, 0, 16383);
    push_event_no_malloc (event);
  }
  void
  push_event_no_malloc (const Event& event)
  {
    if ((events.size() + 1) > events.capacity())
      {
        debug ("event ignored (no space for new event; capacity=%zd)\n", events.capacity());
        return;
      }
    events.push_back (event);
  }
  void process_audio (float **outputs, uint n_frames, uint offset);
  void process (float **outputs, uint n_frames);
  void all_sound_off();
  void system_reset();

  void set_progress_function (std::function<void (double)> function);

  void set_log_function (std::function<void (Log, const char *)> function);
  void set_log_level (Log log_level);
  void logv (Log level, const char *format, va_list vargs) const;

  void error (const char *fmt, ...) const LIQUIDSFZ_PRINTF (2, 3);
  void warning (const char *fmt, ...) const LIQUIDSFZ_PRINTF (2, 3);
  void info (const char *fmt, ...) const LIQUIDSFZ_PRINTF (2, 3);
  void debug (const char *fmt, ...) const LIQUIDSFZ_PRINTF (2, 3);
};

}
