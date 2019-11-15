/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019  Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef LIQUIDSFZ_SYNTH_HH
#define LIQUIDSFZ_SYNTH_HH

#include <random>
#include <algorithm>
#include <assert.h>

#include "loader.hh"
#include "utils.hh"
#include "envelope.hh"
#include "voice.hh"
#include "liquidsfz.hh"

namespace LiquidSFZInternal
{

using LiquidSFZ::Log;

struct Channel
{
  std::vector<uint8_t> cc_values = std::vector<uint8_t> (128, 0);
};

class Synth
{
  std::minstd_rand random_gen;
  std::function<void (Log, const char *)> log_function_;
  std::function<void (double)> progress_function_;
  uint sample_rate_ = 44100; // default
  Loader sfz_loader_;
  uint64_t global_frame_count = 0;
  std::vector<Voice> voices_;
  Log log_level_ = Log::INFO;

  static constexpr int CC_SUSTAIN = 0x40;

public:
  Synth() :
    sfz_loader_ (this)
  {
    // sane defaults:
    set_max_voices (256);
    set_channels (16);
  }
  void
  set_sample_rate (uint sample_rate)
  {
    sample_rate_ = sample_rate;
  }
  void
  set_max_voices (uint n_voices)
  {
    voices_.clear();
    for (uint i = 0; i < n_voices; i++)
      voices_.emplace_back (this);
  }
  void
  set_channels (uint n_channels)
  {
    channels_.clear();
    channels_.resize (n_channels);
  }
  bool
  load (const std::string& filename)
  {
    return sfz_loader_.parse (filename);
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
    for (auto& v : voices_)
      {
        if (v.state_ == Voice::IDLE)
          return &v;
      }
    debug ("alloc_voice: no voices left\n");
    return nullptr;
  }
  uint
  active_voice_count()
  {
    uint c = 0;

    for (auto& v : voices_)
      if (v.state_ != Voice::IDLE)
        c++;
    return c;
  }
  void
  note_on (int chan, int key, int vel)
  {
    /* kill overlapping notes */
    for (auto& voice : voices_)
      {
        if ((voice.state_ == Voice::ACTIVE || voice.state_== Voice::SUSTAIN) &&
            voice.trigger_ == Trigger::ATTACK &&
            voice.channel_ == chan && voice.key_ == key && voice.region_->loop_mode != LoopMode::ONE_SHOT)
          {
            release (voice); // FIXME: we may want to use a fast release here
          }
      }
    trigger_regions (Trigger::ATTACK, chan, key, vel, /* time_since_note_on */ 0.0);
  }

  void
  trigger_regions (Trigger trigger, int chan, int key, int vel, double time_since_note_on)
  {
    // - random must be >= 0.0
    // - random must be <  1.0  (and never 1.0)
    double random = random_gen() / double (random_gen.max() + 1);

    for (auto& region : sfz_loader_.regions)
      {
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

            if (region.play_seq == region.seq_position)
              {
                /* in order to make sequences and random play nice together
                 * random check needs to be done inside sequence check
                 */
                if (region.lorand <= random && region.hirand > random)
                  {
                    if (region.cached_sample)
                      {
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

    for (auto& voice : voices_)
      {
        if (voice.state_ == Voice::ACTIVE &&
            voice.trigger_ == Trigger::ATTACK &&
            voice.channel_ == chan && voice.key_ == key && voice.region_->loop_mode != LoopMode::ONE_SHOT)
          {
            if (sustain_pedal)
              {
                voice.state_ = Voice::SUSTAIN;
              }
            else
              {
                release (voice);
              }
          }
      }
  }
  void
  release (Voice& voice)
  {
    if (voice.state_ != Voice::ACTIVE && voice.state_ != Voice::SUSTAIN)
      {
        error ("release() state %d not active/sustain\n", voice.state_);
        return;
      }
    /* FIXME: random, sequence */
    voice.state_ = Voice::RELEASED;
    voice.envelope_.release();

    double time_since_note_on = (global_frame_count - voice.start_frame_count_) / double (sample_rate_);
    trigger_regions (Trigger::RELEASE, voice.channel_, voice.key_, voice.velocity_, time_since_note_on);
  }
  std::vector<Channel> channels_;
  void
  update_cc (int channel, int controller, int value)
  {
    if (channel < 0 || uint (channel) > channels_.size())
      {
        error ("update_cc: bad channel %d\n", channel);
        return;
      }
    auto& ch = channels_[channel];
    if (controller < 0 || uint (controller) > ch.cc_values.size())
      {
        error ("update_cc: bad channel controller %d\n", controller);
        return;
      }
    ch.cc_values[controller] = value;

    if (controller == CC_SUSTAIN)
      {
        if (value < 0x40)
          {
            for (auto& voice : voices_)
              {
                if (voice.state_ == Voice::SUSTAIN)
                  release (voice);
              }
          }
      }
  }
  int
  get_cc (int channel, int controller)
  {
    if (channel < 0 || uint (channel) > channels_.size())
      {
        error ("get_cc: bad channel %d\n", channel);
        return 0;
      }
    auto& ch = channels_[channel];
    if (controller < 0 || uint (controller) > ch.cc_values.size())
      {
        error ("get_cc: bad channel controller %d\n", controller);
        return 0;
      }
    return ch.cc_values[controller];
  }
  struct Event
  {
    enum class Type : uint16_t { NONE, NOTE_ON, NOTE_OFF, CC };

    uint     time_frames = 0;
    Type     type = Type::NONE;
    uint16_t channel = 0;
    uint16_t arg1 = 0;
    uint16_t arg2 = 0;
  };
  std::vector<Event> events;
  void
  add_event_note_on (uint time_frames, int channel, int key, int velocity)
  {
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
    events.push_back (event);
  }
  void
  add_event_note_off (uint time_frames, int channel, int key)
  {
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::NOTE_OFF;
    event.channel = channel;
    event.arg1 = key;
    event.arg2 = 0;
    events.push_back (event);
  }
  void
  add_event_cc (uint time_frames, int channel, int cc, int value)
  {
    Event event;
    event.time_frames = time_frames;
    event.type = Event::Type::CC;
    event.channel = channel;
    event.arg1 = cc;
    event.arg2 = value;
    events.push_back (event);
  }
  void process_audio (float **outputs, uint n_frames, uint offset);
  void process (float **outputs, uint n_frames);

  void set_progress_function (std::function<void (double)> function);

  void set_log_function (std::function<void (Log, const char *)> function);
  void set_log_level (Log log_level);
  void logv (Log level, const char *format, va_list vargs);

  void error (const char *fmt, ...) LIQUIDSFZ_PRINTF (2, 3);
  void warning (const char *fmt, ...) LIQUIDSFZ_PRINTF (2, 3);
  void info (const char *fmt, ...) LIQUIDSFZ_PRINTF (2, 3);
  void debug (const char *fmt, ...) LIQUIDSFZ_PRINTF (2, 3);
};

}

#endif /* LIQUIDSFZ_SYNTH_HH */
