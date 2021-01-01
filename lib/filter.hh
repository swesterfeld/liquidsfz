/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2020  Stefan Westerfeld
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

#ifndef LIQUIDSFZ_FILTER_HH
#define LIQUIDSFZ_FILTER_HH

#include <cmath>
#include <string>
#include <algorithm>

namespace LiquidSFZInternal
{

class Filter
{
public:
  enum class Type {
    NONE,
    LPF_1P,
    HPF_1P,
    LPF_2P,
    HPF_2P
  };
  static Type
  type_from_string (const std::string& s)
  {
    if (s == "lpf_1p")
      return Type::LPF_1P;

    if (s == "hpf_1p")
      return Type::HPF_1P;

    if (s == "lpf_2p")
      return Type::LPF_2P;

    if (s == "hpf_2p")
      return Type::HPF_2P;

    return Type::NONE;
  }
private:
  bool first = false;
  float last_cutoff = 0;
  float last_resonance = 0;

  /* biquad */
  float a1 = 0;
  float a2 = 0;
  float b0 = 0;
  float b1 = 0;
  float b2 = 0;

  struct BiquadState
  {
    float x1 = 0;
    float x2 = 0;
    float y1 = 0;
    float y2 = 0;
  } b_state_l, b_state_r;

  Type  filter_type_ = Type::NONE;
  int   channels_    = 1;
  int   sample_rate_ = 44100;

  void
  reset_biquad (BiquadState& state)
  {
    state.x1 = 0;
    state.x2 = 0;
    state.y1 = 0;
    state.y2 = 0;
  }
  float
  apply_biquad1p (float in, BiquadState& state) /* degenerate biquad with just one pole */
  {
    float out = b0 * in + b1 * state.x1 - a1 * state.y1;
    state.x1 = in;
    state.y1 = out;
    return out;
  }
  float
  apply_biquad (float in, BiquadState& state)
  {
    float out = b0 * in + b1 * state.x1 + b2 * state.x2 - a1 * state.y1 - a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = in;
    state.y2 = state.y1;
    state.y1 = out;
    return out;
  }
  float
  fast_db_to_factor (float db)
  {
    // usually less expensive than powf (10, db / 20)
    return exp2f (db * 0.166096404744368f);
  }
  void
  update_config (float cutoff, float resonance)
  {
    /* smoothing wouldn't work properly if cutoff is (close to) zero */
    cutoff = std::max (cutoff, 10.f);

    if (first)
      {
        first = false;
      }
    else
      {
        /* parameter smoothing */

        constexpr float high = 1.4;
        constexpr float low = 1. / high;

        cutoff = std::clamp (cutoff, last_cutoff * low, last_cutoff * high);
        resonance = std::clamp (resonance, last_resonance - 1, last_resonance + 1);
      }
    last_cutoff = cutoff;
    last_resonance = resonance;

    float norm_cutoff = std::min (cutoff / sample_rate_, 0.49f);

    if (filter_type_ == Type::LPF_1P) /* 1 pole filter design from DAFX, Zoelzer */
      {
        float k = tanf (M_PI * norm_cutoff);
        float div_factor = 1 / (k + 1);

        b1 = b0 = k * div_factor;
        a1 = (k - 1) * div_factor;
      }
    else if (filter_type_ == Type::HPF_1P)
      {
        float k = tanf (M_PI * norm_cutoff);
        float div_factor = 1 / (k + 1);

        b0 = div_factor;
        b1 = -div_factor;
        a1 = (k - 1) * div_factor;
      }
    else if (filter_type_ == Type::LPF_2P)
      {
        float k = tanf (M_PI * norm_cutoff);
        float kk = k * k;
        float q = fast_db_to_factor (resonance);
        float div_factor = 1  / (1 + (k + 1 / q) * k);

        b0 = kk * div_factor;
        b1 = 2 * kk * div_factor;
        b2 = kk * div_factor;
        a1 = 2 * (kk - 1) * div_factor;
        a2 = (1 - k / q + kk) * div_factor;
      }
    else if (filter_type_ == Type::HPF_2P)
      {
        float k = tanf (M_PI * norm_cutoff);
        float kk = k * k;
        float q = fast_db_to_factor (resonance);
        float div_factor = 1  / (1 + (k + 1 / q) * k);

        b0 = div_factor;
        b1 = -2 * div_factor;
        b2 = div_factor;
        a1 = 2 * (kk - 1) * div_factor;
        a2 = (1 - k / q + kk) * div_factor;
      }
  }
  template<Type T, int C> void
  process_internal (float *left, float *right, float *cutoff, float *resonance, uint n_frames, uint segment_size)
  {
    static_assert (C == 1 || C == 2);

    uint i = 0;
    while (i < n_frames)
      {
        update_config (cutoff[i], resonance[i]);

        uint segment_end = std::min (i + segment_size, n_frames);
        while (i < segment_end)
          {
            if constexpr (T == Type::LPF_1P || T == Type::HPF_1P)
              {
                left[i]  = apply_biquad1p (left[i], b_state_l);
                if constexpr (C == 2)
                  right[i] = apply_biquad1p (right[i], b_state_r);
              }
            if constexpr (T == Type::LPF_2P || T == Type::HPF_2P)
              {
                left[i]  = apply_biquad (left[i], b_state_l);
                if constexpr (C == 2)
                  right[i] = apply_biquad (right[i], b_state_r);
              }
            i++;
          }
      }
  }
  template<int C> void
  process_type (float *left, float *right, float *cutoff, float *resonance, uint n_frames, uint segment_size)
  {
    switch (filter_type_)
    {
      case Type::LPF_1P:  process_internal<Type::LPF_1P, C> (left, right, cutoff, resonance, n_frames, segment_size);
                          break;
      case Type::HPF_1P:  process_internal<Type::HPF_1P, C> (left, right, cutoff, resonance, n_frames, segment_size);
                          break;
      case Type::LPF_2P:  process_internal<Type::LPF_2P, C> (left, right, cutoff, resonance, n_frames, segment_size);
                          break;
      case Type::HPF_2P:  process_internal<Type::HPF_2P, C> (left, right, cutoff, resonance, n_frames, segment_size);
                          break;
      case Type::NONE:    ;
    }
  }
public:
  void
  set_type (Type filter_type)
  {
    filter_type_ = filter_type;
  }
  void
  set_sample_rate (int sample_rate)
  {
    sample_rate_ = sample_rate;
  }
  void
  reset()
  {
    reset_biquad (b_state_l);
    reset_biquad (b_state_r);
    first = true;
  }
  void
  process (float *left, float *right, float cutoff, float resonance, uint n_frames)
  {
    process_type<2> (left, right, &cutoff, &resonance, n_frames, /* just one segment */ n_frames);
  }
  void
  process_mono (float *left, float cutoff, float resonance, uint n_frames)
  {
    process_type<1> (left, nullptr, &cutoff, &resonance, n_frames, /* just one segment */ n_frames);
  }
  void
  process_mod (float *left, float *right, float *cutoff, float *resonance, uint n_frames)
  {
    constexpr uint segment_size = 16; /* subsample control inputs to avoid some of the filter redesign cost */

    process_type<2> (left, right, cutoff, resonance, n_frames, segment_size);
  }
  void
  process_mod_mono (float *left, float *cutoff, float *resonance, uint n_frames)
  {
    constexpr uint segment_size = 16; /* subsample control inputs to avoid some of the filter redesign cost */

    process_type<1> (left, nullptr, cutoff, resonance, n_frames, segment_size);
  }
};

}

#endif /* LIQUIDSFZ_FILTER_HH */
