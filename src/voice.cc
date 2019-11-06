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

#include <math.h>

#include "voice.hh"

using namespace LiquidSFZ;

static double
note_to_freq (int note)
{
  return 440 * exp (log (2) * (note - 69) / 12.0);
}

void
Voice::process (float **outputs, uint nframes)
{
  const auto csample = region->cached_sample;
  const auto channels = csample->channels;
  const double step = double (csample->sample_rate) / sample_rate_ * note_to_freq (key) / note_to_freq (region->pitch_keycenter);

  auto get_samples_mono = [csample, this] (uint x, auto& fsamples)
    {
      for (uint i = 0; i < fsamples.size(); i++)
        {
          if (x >= csample->samples.size())
            {
              fsamples[i] = 0;
            }
          else
            {
              fsamples[i] = csample->samples[x];
              x++;

              if (region->loop_mode == LoopMode::SUSTAIN || region->loop_mode == LoopMode::CONTINUOUS)
                if (x > uint (region->loop_end))
                  x = region->loop_start;
            }
        }
    };

  auto get_samples_stereo = [csample, this] (uint x, auto& fsamples)
    {
      for (uint i = 0; i < fsamples.size(); i += 2)
        {
          if (x >= csample->samples.size())
            {
              fsamples[i]     = 0;
              fsamples[i + 1] = 0;
            }
          else
            {
              fsamples[i]     = csample->samples[x];
              fsamples[i + 1] = csample->samples[x + 1];
              x += 2;

              if (region->loop_mode == LoopMode::SUSTAIN || region->loop_mode == LoopMode::CONTINUOUS)
                if (x > uint (region->loop_end * 2))
                  x = region->loop_start * 2;
            }
        }
    };

  for (uint i = 0; i < nframes; i++)
    {
      const uint ii = ppos;
      const uint x = ii * channels;
      const float frac = ppos - ii;
      if (x < csample->samples.size() && !envelope.done())
        {
          const float amp_gain = envelope.get_next() * (1 / 32768.);
          if (channels == 1)
            {
              std::array<float, 2> fsamples;
              get_samples_mono (x, fsamples);

              const float interp = fsamples[0] * (1 - frac) + fsamples[1] * frac;
              outputs[0][i] += interp * left_gain * amp_gain;
              outputs[1][i] += interp * right_gain * amp_gain;
            }
          else if (channels == 2)
            {
              std::array<float, 4> fsamples;
              get_samples_stereo (x, fsamples);

              outputs[0][i] += (fsamples[0] * (1 - frac) + fsamples[2] * frac) * left_gain * amp_gain;
              outputs[1][i] += (fsamples[1] * (1 - frac) + fsamples[3] * frac) * right_gain * amp_gain;
            }
          else
            {
              assert (false);
            }
        }
      else
        {
          used = false; // FIXME: envelope
        }
      ppos += step;
      if (region->loop_mode == LoopMode::SUSTAIN || region->loop_mode == LoopMode::CONTINUOUS)
        {
          if (region->loop_end > region->loop_start)
            while (ppos > region->loop_end)
              ppos -= (region->loop_end - region->loop_start);
        }
    }
}
