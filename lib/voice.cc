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
Voice::pan_stereo_factor (double region_pan, int ch)
{
  /* sine panning law (constant power panning) */
  const double pan = ch == 0 ? -region_pan : region_pan;
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
  semi_tones += region_->tune * 0.01;
  semi_tones += region_->transpose;

  return exp2f (semi_tones / 12) * region_->cached_sample->sample_rate / sample_rate_;
}

uint
Voice::off_by()
{
  return region_->off_by;
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
  left_gain_.reset (sample_rate, 0.020);
  right_gain_.reset (sample_rate, 0.020);

  velocity_gain_ = velocity_track_factor (region, velocity);
  rt_decay_gain_ = 1.0;
  if (region.trigger == Trigger::RELEASE)
    {
      rt_decay_gain_ = db_to_factor (-time_since_note_on * region.rt_decay);
      synth_->debug ("rt_decay_gain %f\n", rt_decay_gain_);
    }

  update_volume_gain();
  update_amplitude_gain();
  update_pan_gain();
  update_lr_gain (true);

  state_ = ACTIVE;
  envelope_.start (region, sample_rate_);
  synth_->debug ("new voice %s - channels %d\n", region.sample.c_str(), region.cached_sample->channels);
}

void
Voice::update_pan_gain()
{
  float pan = region_->pan;
  if (region_->pan_cc.cc >= 0)
    pan += synth_->get_cc (channel_, region_->pan_cc.cc) * (1 / 127.f) * region_->pan_cc.value;

  pan_left_gain_ = pan_stereo_factor (pan, 0);
  pan_right_gain_ = pan_stereo_factor (pan, 1);
}

void
Voice::update_lr_gain (bool now)
{
  const float global_gain = (1 / 32768.) * synth_->gain() * volume_gain_ * velocity_gain_ * rt_decay_gain_ * amplitude_gain_;

  synth_->debug (" - gain l=%.2f r=%.2f\n", 32768 * pan_left_gain_ * global_gain, 32768 * pan_right_gain_ * global_gain);
  left_gain_.set (pan_left_gain_ * global_gain, now);
  right_gain_.set (pan_right_gain_ * global_gain, now);
}

float
Voice::xfin_gain (int value, int lo, int hi)
{
  if (value < lo)
    return 0;
  if (value < hi && hi > lo)
    return float (value - lo) / (hi - lo);
  else
    return 1;
}

float
Voice::xfout_gain (int value, int lo, int hi)
{
  if (value > hi)
    return 0;
  if (value > lo && hi > lo)
    return 1 - float (value - lo) / (hi - lo);
  else
    return 1;
}

void
Voice::update_volume_gain()
{
  float volume = region_->volume;
  if (region_->gain_cc.cc >= 0)
    volume += synth_->get_cc (channel_, region_->gain_cc.cc) * (1 / 127.f) * region_->gain_cc.value;

  volume_gain_ = db_to_factor (volume);

  volume_gain_ *= xfin_gain (velocity_, region_->xfin_lovel, region_->xfin_hivel);
  volume_gain_ *= xfout_gain (velocity_, region_->xfout_lovel, region_->xfout_hivel);

  volume_gain_ *= xfin_gain (key_, region_->xfin_lokey, region_->xfin_hikey);
  volume_gain_ *= xfout_gain (key_, region_->xfout_lokey, region_->xfout_hikey);

  // xfin_locc / xfin_hicc
  for (auto xfcc : region_->xfin_ccs)
    volume_gain_ *= xfin_gain (synth_->get_cc (channel_, xfcc.cc), xfcc.lo, xfcc.hi);

  // xfout_locc / xfout_hicc
  for (auto xfcc : region_->xfout_ccs)
    volume_gain_ *= xfout_gain (synth_->get_cc (channel_, xfcc.cc), xfcc.lo, xfcc.hi);
}

void
Voice::update_amplitude_gain()
{
  float gain = region_->amplitude * 0.01f;
  if (region_->amplitude_cc.cc >= 0)
    gain *= synth_->get_cc (channel_, region_->amplitude_cc.cc) * (1 / 127.f) * region_->amplitude_cc.value * 0.01f;

  amplitude_gain_ = gain;
}

void
Voice::stop (OffMode off_mode)
{
  state_ = Voice::RELEASED;
  envelope_.stop (off_mode);
}

void
Voice::update_cc (int controller)
{
  if (region_->xfin_ccs.size() || region_->xfout_ccs.size())
    {
      update_volume_gain();
      update_lr_gain (false);
    }
  if (controller == region_->pan_cc.cc)
    {
      update_pan_gain();
      update_lr_gain (false);
    }
  else if (controller == region_->gain_cc.cc)
    {
      update_volume_gain();
      update_lr_gain (false);
    }
  else if (controller == region_->amplitude_cc.cc)
    {
      update_amplitude_gain();
      update_lr_gain (false);
    }
}

void
Voice::update_gain()
{
  update_lr_gain (false);
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

  for (uint i = 0; i < nframes; i++)
    {
      const uint ii = ppos_;
      const uint x = ii * channels;
      const float frac = ppos_ - ii;
      if (x < csample->samples.size() && !envelope_.done())
        {
          const float amp_gain = envelope_.get_next();
          if (channels == 1)
            {
              std::array<float, 2> fsamples;
              get_samples_mono (x, fsamples);

              const float interp = fsamples[0] * (1 - frac) + fsamples[1] * frac;
              outputs[0][i] += interp * amp_gain * left_gain_.get_next();
              outputs[1][i] += interp * amp_gain * right_gain_.get_next();
            }
          else if (channels == 2)
            {
              std::array<float, 4> fsamples;
              get_samples_stereo (x, fsamples);

              outputs[0][i] += (fsamples[0] * (1 - frac) + fsamples[2] * frac) * amp_gain * left_gain_.get_next();
              outputs[1][i] += (fsamples[1] * (1 - frac) + fsamples[3] * frac) * amp_gain * right_gain_.get_next();
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
