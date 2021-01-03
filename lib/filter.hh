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
    HPF_2P,
    BPF_2P,
    BRF_2P,
    LPF_4P,
    HPF_4P,
    LPF_6P,
    HPF_6P
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

    if (s == "bpf_2p")
      return Type::BPF_2P;

    if (s == "brf_2p")
      return Type::BRF_2P;

    if (s == "lpf_4p")
      return Type::LPF_4P;

    if (s == "hpf_4p")
      return Type::HPF_4P;

    if (s == "lpf_6p")
      return Type::LPF_6P;

    if (s == "hpf_6p")
      return Type::HPF_6P;

    return Type::NONE;
  }
  struct CR
  {
    float cutoff;
    float resonance;
    CR (float cutoff, float resonance) :
      cutoff (cutoff),
      resonance (resonance)
    {
    }
  };
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
  };
  BiquadState b_state_a[2];   // 2 pole filter
  BiquadState b_state_b[2];   // 4 pole filter
  BiquadState b_state_z[2];   // 6 pole filter

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
    else if (cutoff == last_cutoff && resonance == last_resonance)
      {
        /* fast case: no need to redesign if parameters didn't change */
        return;
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
    else if (filter_type_ == Type::LPF_2P || filter_type_ == Type::LPF_4P || filter_type_ == Type::LPF_6P) /* 2 pole design DAFX 2nd ed., Zoelzer */
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
    else if (filter_type_ == Type::HPF_2P || filter_type_ == Type::HPF_4P || filter_type_ == Type::HPF_6P)
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
    else if (filter_type_ == Type::BPF_2P)
      {
        float k = tanf (M_PI * norm_cutoff);
        float kk = k * k;
        float q = fast_db_to_factor (resonance);
        float div_factor = 1  / (1 + (k + 1 / q) * k);

        b0 = k / q * div_factor;
        b1 = 0;
        b2 = -b0;
        a1 = 2 * (kk - 1) * div_factor;
        a2 = (1 - k / q + kk) * div_factor;
      }
    else if (filter_type_ == Type::BRF_2P)
      {
        float k = tanf (M_PI * norm_cutoff);
        float kk = k * k;
        float q = fast_db_to_factor (resonance);
        float div_factor = 1  / (1 + (k + 1 / q) * k);

        b0 = (1 + kk) * div_factor;
        b1 = 2 * (kk - 1) * div_factor;
        b2 = b0;
        a1 = 2 * (kk - 1) * div_factor;
        a2 = (1 - k / q + kk) * div_factor;
      }
  }
  template<Type T, class CRFunc, int C> void
  process_internal (float *left, float *right, const CRFunc& cr_func, uint n_frames)
  {
    static_assert (C == 1 || C == 2);

    constexpr uint segment_size = 16;
    uint i = 0;
    while (i < n_frames)
      {
        CR cr = cr_func (i);
        update_config (cr.cutoff, cr.resonance);

        uint segment_end = std::min (i + segment_size, n_frames);
        while (i < segment_end)
          {
            if constexpr (T == Type::LPF_1P || T == Type::HPF_1P)
              {
                left[i]  = apply_biquad1p (left[i], b_state_a[0]);
                if constexpr (C == 2)
                  right[i] = apply_biquad1p (right[i], b_state_a[1]);
              }
            if constexpr (T == Type::LPF_2P || T == Type::HPF_2P || T == Type::BPF_2P || T == Type::BRF_2P)
              {
                left[i]  = apply_biquad (left[i], b_state_a[0]);
                if constexpr (C == 2)
                  right[i] = apply_biquad (right[i], b_state_a[1]);
              }
            if constexpr (T == Type::LPF_4P || T == Type::HPF_4P)
              {
                left[i]  = apply_biquad (apply_biquad (left[i], b_state_a[0]), b_state_b[0]);
                if constexpr (C == 2)
                  right[i] = apply_biquad (apply_biquad (right[i], b_state_a[1]), b_state_b[1]);
              }
            if constexpr (T == Type::LPF_6P || T == Type::HPF_6P)
              {
                left[i]  = apply_biquad (apply_biquad (apply_biquad (left[i], b_state_a[0]), b_state_b[0]), b_state_z[0]);
                if constexpr (C == 2)
                  right[i] = apply_biquad (apply_biquad (apply_biquad (right[i], b_state_a[1]), b_state_b[1]), b_state_z[1]);
              }
            i++;
          }
      }
  }
  template<int C, class CRFunc> void
  process_type (float *left, float *right, const CRFunc& cr_func, uint n_frames)
  {
    switch (filter_type_)
    {
      case Type::LPF_1P:  process_internal<Type::LPF_1P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::HPF_1P:  process_internal<Type::HPF_1P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::LPF_2P:  process_internal<Type::LPF_2P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::HPF_2P:  process_internal<Type::HPF_2P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::BPF_2P:  process_internal<Type::BPF_2P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::BRF_2P:  process_internal<Type::BRF_2P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::LPF_4P:  process_internal<Type::LPF_4P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::HPF_4P:  process_internal<Type::HPF_4P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::LPF_6P:  process_internal<Type::LPF_6P, CRFunc, C> (left, right, cr_func, n_frames);
                          break;
      case Type::HPF_6P:  process_internal<Type::HPF_6P, CRFunc, C> (left, right, cr_func, n_frames);
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
    for (int c = 0; c < 2; c++)
      {
        reset_biquad (b_state_a[c]);
        reset_biquad (b_state_b[c]);
        reset_biquad (b_state_z[c]);
      }
    first = true;
  }
  void
  process (float *left, float *right, float cutoff, float resonance, uint n_frames)
  {
    CR cr (cutoff, resonance);

    process_type<2> (left, right, [cr] (int i) { return cr; }, n_frames);
  }
  void
  process_mono (float *left, float cutoff, float resonance, uint n_frames)
  {
    CR cr (cutoff, resonance);

    process_type<1> (left, nullptr, [cr] (int i) { return cr; }, n_frames);
  }
  void
  process_mod (float *left, float *right, float *cutoff, float *resonance, uint n_frames)
  {
    process_type<2> (left, right, [cutoff, resonance] (int i) { return CR (cutoff[i], resonance[i]); }, n_frames);
  }
  void
  process_mod_mono (float *left, float *cutoff, float *resonance, uint n_frames)
  {
    process_type<1> (left, nullptr, [cutoff, resonance] (int i) { return CR (cutoff[i], resonance[i]); }, n_frames);
  }
  template<class CRFunc> void
  process_mod (float *left, float *right, const CRFunc& cr_func, uint n_frames)
  {
    process_type<2> (left, right, cr_func, n_frames);
  }
  template<class CRFunc> void
  process_mod_mono (float *left, const CRFunc& cr_func, uint n_frames)
  {
    process_type<1> (left, nullptr, cr_func, n_frames);
  }
};

}

#endif /* LIQUIDSFZ_FILTER_HH */
