// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <cmath>
#include <cassert>
#include <string>
#include <algorithm>
#include <array>

typedef unsigned int uint;

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
    HPF_6P,
    PEQ
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

    if (s == "peq")
      return Type::PEQ;

    return Type::NONE;
  }
  static constexpr int
  filter_order (Type t)
  {
    switch (t)
    {
      case Type::NONE:
        return 0;
      case Type::LPF_1P:
      case Type::HPF_1P:
        return 1;
      case Type::LPF_2P:
      case Type::HPF_2P:
      case Type::BPF_2P:
      case Type::BRF_2P:
      case Type::PEQ:
        return 2;
      case Type::LPF_4P:
      case Type::HPF_4P:
        return 4;
      case Type::LPF_6P:
      case Type::HPF_6P:
        return 6;
    }
    return 0;
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
  // Helper functions for bandwidth to Q conversion
  static float
  convert_bw_to_Q (float bandwidth_octaves)
  {
    // Simple analog approximation: Q = 1 / (2 * sinh(ln(2)/2 * bw))
    const float ln2_half = 0.34657359027997f; // ln(2)/2 ≈ 0.346573
    return 1.0f / (2.0f * sinhf (ln2_half * bandwidth_octaves));
  }
  static float
  convert_bw_to_Q_freq_dependent (float bandwidth_octaves, float freq_hz, float sample_rate)
  {
    // Frequency-dependent Audio EQ Cookbook formula with BLT conversion
    // Q = 0.5 / sinh(ln(2)/2 * bandwidth * ω₀/sin(ω₀))
    const float w = 2.0f * M_PI * std::min (freq_hz / sample_rate, 0.49f);
    const float sin_w = sinf (w);
    const float ln2_half = 0.34657359027997f; // ln(2)/2 ≈ 0.346573
    const float sinh_arg = ln2_half * bandwidth_octaves * w / sin_w;
    const float sinh_val = sinhf (sinh_arg);
    return std::max (0.001f, 0.5f / sinh_val);
  }
private:
  bool first = false;
  float last_cutoff = 0;
  float last_resonance = 0;
  float last_peq_freq = 0;
  float last_peq_Q = 0;
  float last_peq_gain = 0;
  uint config_count_down = 0;

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
  BiquadState b_state[3][2];

  Type  filter_type_ = Type::NONE;
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
  template<Type T>
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

        float cutoff_smooth;
        float reso_smooth;
        if constexpr (filter_order (T) == 6)
          {
            cutoff_smooth = 1.05;
            reso_smooth   = 0.33;
          }
        else if constexpr (filter_order (T) == 4)
          {
            cutoff_smooth = 1.1;
            reso_smooth   = 0.5;
          }
        else // order 2 or lower
          {
            cutoff_smooth = 1.2;
            reso_smooth   = 1;
          }
        const float high = cutoff_smooth;
        const float low = 1. / high;

        cutoff = std::clamp (cutoff, last_cutoff * low, last_cutoff * high);
        resonance = std::clamp (resonance, last_resonance - reso_smooth, last_resonance + reso_smooth);
      }
    last_cutoff = cutoff;
    last_resonance = resonance;

    float norm_cutoff = std::min (cutoff / sample_rate_, 0.49f);

    if (filter_order (T) == 1) /* 1 pole filter design from DAFX, Zoelzer */
      {
        const float k = tanf (M_PI * norm_cutoff);
        const float div_factor = 1 / (k + 1);

        a1 = (k - 1) * div_factor;

        if (T == Type::LPF_1P)
          {
            b0 = k * div_factor;
            b1 = b0;
          }
        else if (T == Type::HPF_1P)
          {
            b0 = div_factor;
            b1 = -div_factor;
          }
      }
    else /* 2 pole design DAFX 2nd ed., Zoelzer */
      {
        const float k = tanf (M_PI * norm_cutoff);
        const float kk = k * k;
        const float q = fast_db_to_factor (resonance);
        const float div_factor = 1  / (1 + (k + 1 / q) * k);

        a1 = 2 * (kk - 1) * div_factor;
        a2 = (1 - k / q + kk) * div_factor;

        if (T == Type::LPF_2P || T == Type::LPF_4P || T == Type::LPF_6P)
          {
            b0 = kk * div_factor;
            b1 = 2 * b0;
            b2 = b0;
          }
        else if (T == Type::HPF_2P || T == Type::HPF_4P || T == Type::HPF_6P)
          {
            b0 = div_factor;
            b1 = -2 * div_factor;
            b2 = div_factor;
          }
        else if (T == Type::BPF_2P)
          {
            b0 = k / q * div_factor;
            b1 = 0;
            b2 = -b0;
          }
        else if (T == Type::BRF_2P)
          {
            b0 = (1 + kk) * div_factor;
            b1 = 2 * (kk - 1) * div_factor;
            b2 = b0;
          }
      }
  }
  void
  update_config_peq (float freq, float Q, float gain_db)
  {
    /* smoothing wouldn't work properly if freq is (close to) zero */
    freq = std::max (freq, 10.f);

    if (first)
      {
        first = false;
      }
    else if (freq == last_peq_freq && Q == last_peq_Q && gain_db == last_peq_gain)
      {
        /* fast case: no need to redesign if parameters didn't change */
        return;
      }
    else
      {
        /* parameter smoothing */
        const float freq_smooth = 1.2;
        const float Q_smooth = 1.2;
        const float gain_smooth = 0.5;

        const float high = freq_smooth;
        const float low = 1. / high;

        const float highQ = Q_smooth;
        const float lowQ = 1. / highQ;

        freq = std::clamp (freq, last_peq_freq * low, last_peq_freq * high);
        Q = std::clamp (Q, last_peq_Q * lowQ, last_peq_Q * highQ);
        gain_db = std::clamp (gain_db, last_peq_gain - gain_smooth, last_peq_gain + gain_smooth);
      }
    last_peq_freq = freq;
    last_peq_Q = Q;
    last_peq_gain = gain_db;

    float norm_freq = std::min (freq / sample_rate_, 0.49f);

    // Peaking EQ design from "Audio EQ Cookbook" by Robert Bristow-Johnson
    const float w = 2.0f * M_PI * norm_freq;
    const float cos_w = cosf (w);
    const float sin_w = sinf (w);
    const float A = fast_db_to_factor (gain_db / 2.0f); // sqrt of linear gain

    // Use Q directly - no conversion needed since loader converts BW to Q
    const float Q_clamped = std::max (Q, 0.001f); // Prevent division by zero
    const float alpha = sin_w / (2.0f * Q_clamped);

    // Audio EQ Cookbook peaking EQ coefficients (equation 27)
    const float b0_norm = 1.0f + alpha * A;
    const float b1_norm = -2.0f * cos_w;
    const float b2_norm = 1.0f - alpha * A;
    const float a0_norm = 1.0f + alpha / A;
    const float a1_norm = -2.0f * cos_w;
    const float a2_norm = 1.0f - alpha / A;

    const float inv_a0 = 1.0f / a0_norm;

    b0 = b0_norm * inv_a0;
    b1 = b1_norm * inv_a0;
    b2 = b2_norm * inv_a0;
    a1 = a1_norm * inv_a0;
    a2 = a2_norm * inv_a0;
  }

  template<Type T, int S, int C>
  void
  process_biquad (float *left, float *right, uint n_frames)
  {
    BiquadState sl = b_state[S][0], sr = b_state[S][1];

    for (uint i = 0; i < n_frames; i++)
      {
        if (filter_order (T) == 1)
          {
            left[i]  = apply_biquad1p (left[i], sl);
            if constexpr (C == 2)
              right[i] = apply_biquad1p (right[i], sr);
          }
        else
          {
            left[i] = apply_biquad (left[i], sl);
            if constexpr (C == 2)
              right[i] = apply_biquad (right[i], sr);
          }
      }

    b_state[S][0] = sl;
    b_state[S][1] = sr;
  }
  template<Type T, class CRFunc, int C> void
  process_internal (float *left, float *right, const CRFunc& cr_func, uint n_frames)
  {
    static_assert (C == 1 || C == 2);

    uint i = 0;
    while (i < n_frames)
      {
        if (config_count_down == 0)
          {
            CR cr = cr_func (i);
            update_config<T> (cr.cutoff, cr.resonance);

            config_count_down = 16;
          }

        uint todo = std::min (config_count_down, n_frames - i);
        if constexpr (filter_order (T) == 1)
          {
            process_biquad<T, 0, C> (left + i, right + i, todo);
          }
        if constexpr (filter_order (T) == 2)
          {
            process_biquad<T, 0, C> (left + i, right + i, todo);
          }
        if constexpr (filter_order (T) == 4)
          {
            process_biquad<T, 0, C> (left + i, right + i, todo);
            process_biquad<T, 1, C> (left + i, right + i, todo);
          }
        if constexpr (filter_order (T) == 6)
          {
            process_biquad<T, 0, C> (left + i, right + i, todo);
            process_biquad<T, 1, C> (left + i, right + i, todo);
            process_biquad<T, 2, C> (left + i, right + i, todo);
          }
        i += todo;
        config_count_down -= todo;
      }
  }
  template<int C>
  void
  process_peq (float *left, float *right, float freq, float Q, float gain_db, uint n_frames)
  {
    static_assert (C == 1 || C == 2);
    uint i = 0;

    while (i < n_frames)
      {
        if (config_count_down == 0)
          {
            update_config_peq (freq, Q, gain_db);

            config_count_down = 16;
          }
        uint todo = std::min (config_count_down, n_frames - i);

        process_biquad<Type::PEQ, 0, C> (left + i, right + i, todo);

        i += todo;
        config_count_down -= todo;
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
      case Type::PEQ:     assert (false);
    }
  }
public:
  void
  reset (Type filter_type, int sample_rate)
  {
    for (int s = 0; s < 3; s++)
      {
        for (int c = 0; c < 2; c++)
          {
            reset_biquad (b_state[s][c]);
            reset_biquad (b_state[s][c]);
            reset_biquad (b_state[s][c]);
          }
      }
    first = true;
    config_count_down = 0;
    filter_type_ = filter_type;
    sample_rate_ = sample_rate;
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
  void
  process_peq (float *left, float *right, float freq, float Q, float gain_db, uint n_frames)
  {
    process_peq<2> (left, right, freq, Q, gain_db, n_frames);
  }
  void
  process_peq_mono (float *left, float freq, float Q, float gain_db, uint n_frames)
  {
    process_peq<1> (left, nullptr, freq, Q, gain_db, n_frames);
  }
};

}
