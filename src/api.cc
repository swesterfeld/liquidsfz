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

#include "api.hh"
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
Synth::add_event_note_on (uint offset, int channel, int key, int velocity)
{
  impl->synth.add_event_note_on (offset, channel, key, velocity);
}

void
Synth::add_event_note_off (uint offset, int channel, int key)
{
  impl->synth.add_event_note_off (offset, channel, key);
}

void
Synth::add_event_cc (uint offset, int channel, int cc, int value)
{
  impl->synth.add_event_cc (offset, channel, cc, value);
}

void
Synth::process (float **outputs, uint nframes)
{
  impl->synth.process (outputs, nframes);
}
