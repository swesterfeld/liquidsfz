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
#include "synth.hh"

using namespace LiquidSFZInternal;

double
Voice::pan_stereo_factor (const Region& r, int ch)
{
  /* sine panning law (constant power panning) */
  const double pan = ch == 0 ? -r.pan : r.pan;
  return sin ((pan + 100) / 400 * M_PI);
}

double
Voice::velocity_track_factor (const Region& r, int midi_velocity)
{
  double curve = (midi_velocity * midi_velocity) / (127.0 * 127.0);
  double veltrack_factor = r.amp_veltrack * 0.01;

  double offset = (veltrack_factor >= 0) ? 1 : 0;
  double v = (offset - veltrack_factor) + veltrack_factor * curve;

  return v;
}

double
Voice::replay_speed()
{
  double semi_tones = (key_ - region_->pitch_keycenter) * (region_->pitch_keytrack * 0.01);

  return exp2f (semi_tones / 12) * region_->cached_sample->sample_rate / sample_rate_;
}

void
Voice::start (const Region& region, int channel, int key, int velocity, double time_since_note_on, uint64_t global_frame_count, uint sample_rate)
{
  start_frame_count_ = global_frame_count;
  sample_rate_ = sample_rate;
  region_ = &region;
  channel_ = channel;
  key_ = key;
  velocity_ = velocity;
  ppos_ = 0;
  trigger_ = region.trigger;
  double volume_gain = db_to_factor (region.volume);
  double velocity_gain = velocity_track_factor (region, velocity);
  double rt_decay_gain = 1.0;
  if (region.trigger == Trigger::RELEASE)
    {
      rt_decay_gain = db_to_factor (-time_since_note_on * region.rt_decay);
      synth_->debug ("rt_decay_gain %f\n", rt_decay_gain);
    }
  left_gain_ = velocity_gain * volume_gain * rt_decay_gain * pan_stereo_factor (region, 0);
  right_gain_ = velocity_gain * volume_gain * rt_decay_gain * pan_stereo_factor (region, 1);
  state_ = ACTIVE;
  envelope_.start (region, sample_rate_);
  synth_->debug ("new voice %s - channels %d\n", region.sample.c_str(), region.cached_sample->channels);
}

void
Voice::process (float **outputs, uint nframes)
{
  const auto csample = region_->cached_sample;
  const auto channels = csample->channels;
  const double step = replay_speed();

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

              if (region_->loop_mode == LoopMode::SUSTAIN || region_->loop_mode == LoopMode::CONTINUOUS)
                if (x > uint (region_->loop_end))
                  x = region_->loop_start;
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

              if (region_->loop_mode == LoopMode::SUSTAIN || region_->loop_mode == LoopMode::CONTINUOUS)
                if (x > uint (region_->loop_end * 2))
                  x = region_->loop_start * 2;
            }
        }
    };

  const float global_gain = (1 / 32768.) * synth_->gain();
  for (uint i = 0; i < nframes; i++)
    {
      const uint ii = ppos_;
      const uint x = ii * channels;
      const float frac = ppos_ - ii;
      if (x < csample->samples.size() && !envelope_.done())
        {
          const float amp_gain = envelope_.get_next() * global_gain;
          if (channels == 1)
            {
              std::array<float, 2> fsamples;
              get_samples_mono (x, fsamples);

              const float interp = fsamples[0] * (1 - frac) + fsamples[1] * frac;
              outputs[0][i] += interp * left_gain_ * amp_gain;
              outputs[1][i] += interp * right_gain_ * amp_gain;
            }
          else if (channels == 2)
            {
              std::array<float, 4> fsamples;
              get_samples_stereo (x, fsamples);

              outputs[0][i] += (fsamples[0] * (1 - frac) + fsamples[2] * frac) * left_gain_ * amp_gain;
              outputs[1][i] += (fsamples[1] * (1 - frac) + fsamples[3] * frac) * right_gain_ * amp_gain;
            }
          else
            {
              assert (false);
            }
        }
      else
        {
          state_ = IDLE;
          break;
        }
      ppos_ += step;
      if (region_->loop_mode == LoopMode::SUSTAIN || region_->loop_mode == LoopMode::CONTINUOUS)
        {
          if (region_->loop_end > region_->loop_start)
            while (ppos_ > region_->loop_end)
              ppos_ -= (region_->loop_end - region_->loop_start);
        }
    }
}
