/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2021  Stefan Westerfeld
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

#ifndef LIQUIDSFZ_LFOGEN_HH
#define LIQUIDSFZ_LFOGEN_HH

#include "utils.hh"

namespace LiquidSFZInternal
{

class LFOGen
{
public:
  enum OutputType {
    PITCH  = 0,
    VOLUME = 1,
    CUTOFF = 2
  };

private:
  Synth *synth_ = nullptr;
  int channel_ = 0;
  int sample_rate_ = 0;

  struct LFO {
    const LFOParams *params = nullptr;
    double phase = 0;
    float next_freq_mod = 0;
    float freq_mod = 0;
    float freq = 0;
    float to_pitch = 0;
    float to_volume = 0;
    float to_cutoff = 0;
    uint  delay_len = 0;
    uint  fade_len = 0;
    uint  fade_pos = 0;
    struct Target
    {
      float *target;
      float  multiply;
    };
    std::vector<Target> targets;
  };
  struct Output
  {
    bool    active     = false;
    float  *buffer    = nullptr;
    float   last_value = 0;
    float   value      = 0;
  };
  std::array<Output, 3> outputs;
  bool first = false;
  std::vector<LFO> lfos;

  void process_lfo (LFO& lfo, uint n_values);

  void
  smooth (OutputType type, uint start, uint n_values)
  {
    if (!outputs[type].active)
      return;

    float last_value = outputs[type].last_value;
    float value      = outputs[type].value;
    float *out       = outputs[type].buffer + start;
    for (uint k = 0; k < n_values; k++)
      {
        last_value = value * 0.01f + 0.99f * last_value;
        out[k] = last_value;
      }
    outputs[type].last_value = last_value;
  }

public:
  LFOGen (Synth *synth) :
    synth_ (synth)
  {
  }
  void start (const Region& region, int channel, int sample_rate);
  void process (float *buffer, uint n_values);

  const float *
  get (OutputType type)
  {
    return outputs[type].buffer;
  }
  uint
  buffer_size (uint n_values)
  {
    return outputs.size() * n_values;
  }
};

}

#endif /* LIQUIDSFZ_LFOGEN_HH */
