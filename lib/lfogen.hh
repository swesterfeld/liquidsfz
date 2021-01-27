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
  float smoothing_factor_ = 0;

  struct Wave;

  /* modulation links */
  struct ModLink
  {
    float *source;
    float  factor;
    float *dest;
  };
  struct LFO {
    const LFOParams *params = nullptr;
    Synth *synth = nullptr;
    float phase = 0;
    Wave *wave = nullptr;
    float next_freq_mod = 0;
    float freq_mod = 0;
    float freq = 0;
    float value = 0;
    float to_pitch = 0;
    float to_volume = 0;
    float to_cutoff = 0;
    uint  delay_len = 0;
    uint  fade_len = 0;
    uint  fade_pos = 0;

    /* sample and hold wave form */
    float sh_value = 0;
    int   last_sh_state = -1;
  };
  struct Output
  {
    bool    active     = false;
    float  *buffer    = nullptr;
    float   last_value = 0;
    float   value      = 0;
  };
  struct Wave
  {
    virtual float eval (LFO& lfo) = 0;
  };
  static Wave *get_wave (int wave);

  std::array<Output, 3> outputs;
  bool first = false;
  std::vector<LFO> lfos;
  std::vector<ModLink> mod_links;

  void process_lfo (LFO& lfo, uint n_values);

  template<OutputType T>
  float
  post_function (float value);

  template<OutputType T>
  void
  write_output (uint start, uint n_values);
public:
  LFOGen (Synth *synth, const Limits& limits);

  void start (const Region& region, int channel, int sample_rate);
  void process (float *buffer, uint n_values);
  void update_ccs();

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
  static bool supports_wave (int wave);
};

}

#endif /* LIQUIDSFZ_LFOGEN_HH */
