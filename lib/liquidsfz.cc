// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "liquidsfz.hh"
#include "synth.hh"

using namespace LiquidSFZ;

using std::vector;
using std::string;

struct Synth::Impl {
  LiquidSFZInternal::Synth synth;
};

Synth::Synth() : impl (new Synth::Impl())
{
}

Synth::~Synth()
{
}

void
Synth::set_sample_rate (uint sample_rate)
{
  impl->synth.set_sample_rate (sample_rate);
}

uint
Synth::sample_rate() const
{
  return impl->synth.sample_rate();
}

void
Synth::set_max_voices (uint n_voices)
{
  impl->synth.set_max_voices (n_voices);
}

uint
Synth::max_voices() const
{
  return impl->synth.max_voices();
}

void
Synth::set_live_mode (bool live_mode)
{
  impl->synth.set_live_mode (live_mode);
}

bool
Synth::live_mode() const
{
  return impl->synth.live_mode();
}

void
Synth::set_preload_time (uint time_ms)
{
  impl->synth.set_preload_time (time_ms);
}

uint
Synth::preload_time() const
{
  return impl->synth.preload_time();
}

void
Synth::set_sample_quality (int sample_quality)
{
  impl->synth.set_sample_quality (sample_quality);
}

int
Synth::sample_quality()
{
  return impl->synth.sample_quality();
}

uint
Synth::active_voice_count() const
{
  return impl->synth.active_voice_count();
}

void
Synth::set_gain (float gain)
{
  impl->synth.set_gain (gain);
}

bool
Synth::load (const std::string& filename)
{
  return impl->synth.load (filename);
}

bool
Synth::is_bank (const std::string& filename)
{
  return impl->synth.is_bank (filename);
}

bool
Synth::load_bank (const std::string& filename)
{
  return impl->synth.load_bank (filename);
}

bool
Synth::select_program (uint program)
{
  return impl->synth.select_program (program);
}

void
Synth::add_event_note_on (uint time_frames, int channel, int key, int velocity)
{
  impl->synth.add_event_note_on (time_frames, channel, key, velocity);
}

void
Synth::add_event_note_off (uint time_frames, int channel, int key)
{
  impl->synth.add_event_note_off (time_frames, channel, key);
}

void
Synth::add_event_cc (uint time_frames, int channel, int cc, int value)
{
  impl->synth.add_event_cc (time_frames, channel, cc, value);
}

void
Synth::add_event_pitch_bend (uint time_frames, int channel, int value)
{
  impl->synth.add_event_pitch_bend (time_frames, channel, value);
}

void
Synth::process (float **outputs, uint nframes)
{
  impl->synth.process (outputs, nframes);
}

void
Synth::all_sound_off()
{
  impl->synth.all_sound_off();
}

void
Synth::system_reset()
{
  impl->synth.system_reset();
}

void
Synth::set_log_level (Log log_level)
{
  impl->synth.set_log_level (log_level);
}

void
Synth::set_log_function (std::function<void (Log, const char *)> function)
{
  impl->synth.set_log_function (function);
}

void
Synth::set_progress_function (std::function<void (double)> progress_function)
{
  impl->synth.set_progress_function (progress_function);
}

size_t
Synth::cache_size() const
{
  return impl->synth.cache_size();
}

uint
Synth::cache_file_count() const
{
  return impl->synth.cache_file_count();
}

void
Synth::set_max_cache_size (size_t max_cache_size)
{
  impl->synth.set_max_cache_size (max_cache_size);
}

size_t
Synth::max_cache_size() const
{
  return impl->synth.max_cache_size();
}

/*----------------- ProgramInfo --------------*/

struct ProgramInfo::Impl {
  LiquidSFZInternal::ProgramInfo program_info;
};

ProgramInfo::ProgramInfo() : impl (new ProgramInfo::Impl())
{
}

ProgramInfo::~ProgramInfo()
{
}

int
ProgramInfo::index() const
{
  return impl->program_info.index;
}

string
ProgramInfo::label() const
{
  return impl->program_info.name;
}

vector<ProgramInfo>
Synth::list_programs() const
{
  vector<ProgramInfo> result;

  for (const auto& program_info : impl->synth.list_programs())
    {
      auto& program_result = result.emplace_back();
      program_result.impl->program_info = program_info;
    }
  return result;
}

/*----------------- CCInfo --------------*/

struct CCInfo::Impl {
  LiquidSFZInternal::CCInfo cc_info;
};

CCInfo::CCInfo() : impl (new CCInfo::Impl())
{
}

CCInfo::~CCInfo()
{
}

int
CCInfo::cc() const
{
  return impl->cc_info.cc;
}

string
CCInfo::label() const
{
  if (impl->cc_info.has_label)
    return impl->cc_info.label;
  else
    return LiquidSFZInternal::string_printf ("CC%03d", impl->cc_info.cc);
}

bool
CCInfo::has_label() const
{
  return impl->cc_info.has_label;
}

int
CCInfo::default_value() const
{
  return impl->cc_info.default_value;
}

vector<CCInfo>
Synth::list_ccs() const
{
  vector<CCInfo> result;

  for (const auto& cc_info : impl->synth.list_ccs())
    {
      auto& cc_result = result.emplace_back();
      cc_result.impl->cc_info = cc_info;
    }
  return result;
}

/*----------------- KeyInfo --------------*/

struct KeyInfo::Impl {
  LiquidSFZInternal::KeyInfo key_info;
};

KeyInfo::KeyInfo() : impl (new KeyInfo::Impl())
{
}

KeyInfo::~KeyInfo()
{
}

int
KeyInfo::key() const
{
  return impl->key_info.key;
}

string
KeyInfo::label() const
{
  return impl->key_info.label;
}

bool
KeyInfo::is_switch() const
{
  return impl->key_info.is_switch;
}

vector<KeyInfo>
Synth::list_keys() const
{
  vector<KeyInfo> result;

  for (const auto& key_info : impl->synth.list_keys())
    {
      auto& key_result = result.emplace_back();
      key_result.impl->key_info = key_info;
    }
  return result;
}
