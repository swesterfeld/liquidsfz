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

#include "liquidsfz.hh"
#include "synth.hh"

using namespace LiquidSFZ;

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

void
Synth::set_max_voices (uint n_voices)
{
  impl->synth.set_max_voices (n_voices);
}

bool
Synth::load (const std::string& filename)
{
  return impl->synth.load (filename);
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
Synth::process (float **outputs, uint nframes)
{
  impl->synth.process (outputs, nframes);
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