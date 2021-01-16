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
  Synth *synth_ = nullptr;
  int channel_ = 0;
  int sample_rate_ = 0;

  struct State {
    const LFOParams *params = nullptr;
    double phase = 0;
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
  bool first = false;
  float last_speed_factor  = 0;
  float last_volume_factor = 0;
  float last_cutoff_factor = 0;
  std::vector<State> lfos;

public:
  LFOGen (Synth *synth) :
    synth_ (synth)
  {
  }
  void start (const Region& region, int channel, int sample_rate);
  void process (float *lfo_speed_factor,
                float *lfo_volume_factor,
                float *lfo_cutoff_factor,
                uint   n_frames);
};

}

#endif /* LIQUIDSFZ_LFOGEN_HH */
