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

namespace LiquidSFZInternal
{

class Filter
{
public:
  enum class Type {
    NONE,
    LPF_1P,
    HPF_1P
  };
  static Type
  type_from_string (const std::string& s)
  {
    if (s == "lpf_1p")
      return Type::LPF_1P;

    if (s == "hpf_1p")
      return Type::HPF_1P;

    return Type::NONE;
  }
private:
  float p = 0;
  float tmp_l = 0;
  float tmp_r = 0;

  Type  filter_type_ = Type::NONE;
  int   channels_    = 1;
  int   sample_rate_ = 44100;
public:
  void
  set_type (Type filter_type)
  {
    filter_type_ = filter_type;
  }
  void
  set_channels (int channels)
  {
    channels_ = channels;
  }
  void
  set_sample_rate (int sample_rate)
  {
    sample_rate_ = sample_rate;
  }
/* one pole filter musicdsp */
#if 0
recursion: tmp = (1-p)*in + p*tmp with output = tmp
coefficient: p = (2-cos(x)) - sqrt((2-cos(x))^2 - 1) with x = 2*pi*cutoff/samplerate
coeficient approximation: p = (1 - 2*cutoff/samplerate)^2

HP:
recursion: tmp = (p-1)*in - p*tmp with output = tmp
coefficient: p = (2+cos(x)) - sqrt((2+cos(x))^2 - 1) with x = 2*pi*cutoff/samplerate
coeficient approximation: p = (2*cutoff/samplerate)^2

recursion: tmp = (1-p)*in + p*tmp with output = in-tmp (corrected version)
coefficient: p = (2-cos(x)) - sqrt((2-cos(x))^2 - 1) with x = 2*pi*cutoff/samplerate
coeficient approximation: p = (1 - 2*cutoff/samplerate)^2

#endif
  void
  reset (float cutoff)
  {
    if (filter_type_ == Type::LPF_1P)
      {
        double x = 2 * M_PI * cutoff / sample_rate_;
        p = (2 - cos (x)) - sqrt ((2 - cos(x)) * (2 - cos (x)) - 1);
      }
    else if (filter_type_ == Type::HPF_1P)
      {
        double x = 2 * M_PI * cutoff / sample_rate_;
        p = (2 - cos (x)) - sqrt ((2 - cos(x)) * (2 - cos (x)) - 1);
      }

    tmp_l = 0;
    tmp_r = 0;
  }
  void
  process (float *left, float *right, uint n_frames)
  {
    if (filter_type_ == Type::LPF_1P)
      {
        for (uint i = 0; i < n_frames; i++)
          {
            left[i]  = tmp_l = (1 - p) * left[i]  + p * tmp_l;
            right[i] = tmp_r = (1 - p) * right[i] + p * tmp_r;
          }
      }
    if (filter_type_ == Type::HPF_1P)
      {
        for (uint i = 0; i < n_frames; i++)
          {
            tmp_l = (1 - p) * left[i] + p * tmp_l;
            tmp_r = (1 - p) * right[i] + p * tmp_r;
            left[i] -= tmp_l;
            right[i] -= tmp_r;
          }
      }
  }
};

}

#endif /* LIQUIDSFZ_FILTER_HH */
