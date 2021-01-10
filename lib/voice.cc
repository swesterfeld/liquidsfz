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

#include <array>

#include "voice.hh"
#include "synth.hh"

using namespace LiquidSFZInternal;

using std::clamp;

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
  double curve;
  if (r.amp_velcurve.empty())
    curve = (midi_velocity * midi_velocity) / (127.0 * 127.0);
  else
    curve = r.amp_velcurve.get (midi_velocity);

  double veltrack_factor = r.amp_veltrack * 0.01;

  double offset = (veltrack_factor >= 0) ? 1 : 0;
  double v = (offset - veltrack_factor) + veltrack_factor * curve;

  return v;
}

void
Voice::update_replay_speed (bool now)
{
  double semi_tones = (key_ - region_->pitch_keycenter) * (region_->pitch_keytrack * 0.01);
  semi_tones += (region_->tune + pitch_random_cent_) * 0.01;
  semi_tones += region_->transpose;

  /* pitch bend */
  if (pitch_bend_value_ >= 0)
    semi_tones += pitch_bend_value_ * (region_->bend_up * 0.01);
  else
    semi_tones += pitch_bend_value_ * (region_->bend_down * -0.01);

  semi_tones += synth_->get_cc_vec_value (channel_, region_->tune_cc) * 0.01; // tune_oncc

  replay_speed_.set (exp2f (semi_tones / 12) * region_->cached_sample->sample_rate / sample_rate_, now);
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
  trigger_ = region.trigger;
  left_gain_.reset (sample_rate, 0.020);
  right_gain_.reset (sample_rate, 0.020);
  replay_speed_.reset (sample_rate, 0.020);

  amp_random_gain_ = db_to_factor (region.amp_random * synth_->normalized_random_value());
  pitch_random_cent_ = region.pitch_random * synth_->normalized_random_value();

  velocity_gain_ = velocity_track_factor (region, velocity);
  rt_decay_gain_ = 1.0;
  if (region.trigger == Trigger::RELEASE)
    {
      rt_decay_gain_ = db_to_factor (-time_since_note_on * region.rt_decay);
      synth_->debug ("rt_decay_gain %f\n", rt_decay_gain_);
    }

  double delay = region.delay;
  delay += synth_->get_cc_vec_value (channel_, region_->delay_cc); // delay_oncc
  delay_samples_ = std::max (delay * sample_rate, 0.0);

  /* loop? */
  loop_enabled_ = false;
  if (region_->loop_mode == LoopMode::SUSTAIN || region_->loop_mode == LoopMode::CONTINUOUS)
    {
      if (region_->loop_end > region_->loop_start)
        loop_enabled_ = true;
    }

  /* play start position */
  uint offset = region.offset;
  offset += lrint (region.offset_random * synth_->normalized_random_value());
  offset += lrint (synth_->get_cc_vec_value (channel_, region.offset_cc));
  ppos_ = offset;
  if (ppos_ > region.loop_end)
    loop_enabled_ = false;

  update_volume_gain();
  update_amplitude_gain();
  update_pan_gain();
  update_lr_gain (true);

  set_pitch_bend (synth_->get_pitch_bend (channel));
  update_replay_speed (true);

  const float vnorm = velocity * (1 / 127.f);
  envelope_.set_delay (amp_value (vnorm, region.ampeg_delay));
  envelope_.set_attack (amp_value (vnorm, region.ampeg_attack));
  envelope_.set_hold (amp_value (vnorm, region.ampeg_hold));
  envelope_.set_decay (amp_value (vnorm, region.ampeg_decay));
  envelope_.set_sustain (amp_value (vnorm, region.ampeg_sustain));
  envelope_.set_release (amp_value (vnorm, region.ampeg_release));
  envelope_.start (region, sample_rate_);

  state_ = ACTIVE;
  synth_->debug ("location %s\n", region.location.c_str());
  synth_->debug ("new voice %s - channels %d\n", region.sample.c_str(), region.cached_sample->channels);

  filter_envelope_.set_shape (Envelope::Shape::LINEAR);
  filter_envelope_.set_delay (amp_value (vnorm, region.fileg_delay));
  filter_envelope_.set_attack (amp_value (vnorm, region.fileg_attack));
  filter_envelope_.set_hold (amp_value (vnorm, region.fileg_hold));
  filter_envelope_.set_decay (amp_value (vnorm, region.fileg_decay));
  filter_envelope_.set_sustain (amp_value (vnorm, region.fileg_sustain));
  filter_envelope_.set_release (amp_value (vnorm, region.fileg_release));
  filter_envelope_.start (region, sample_rate_);

  filter_envelope_depth_ = amp_value (vnorm, region.fileg_depth);

  start_filter (fimpl_, &region.fil);
  start_filter (fimpl2_, &region.fil2);
}

void
Voice::start_filter (FImpl& fi, const FilterParams *params)
{
  fi.params = params;

  fi.filter.reset (params->type, sample_rate_);

  fi.cutoff_smooth.reset (sample_rate_, 0.005);
  fi.resonance_smooth.reset (sample_rate_, 0.005);

  update_cutoff (fi, true);
  update_resonance (fi, true);
}

float
Voice::amp_value (float vnorm, const EGParam& amp_param)
{
  float value = amp_param.base + amp_param.vel2 * vnorm;
  value += synth_->get_cc_vec_value (channel_, amp_param.cc_vec);

  return value;
}

void
Voice::update_pan_gain()
{
  float pan = region_->pan;
  pan += synth_->get_cc_vec_value (channel_, region_->pan_cc);
  pan = clamp (pan, -100.f, 100.f);

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
Voice::apply_xfcurve (float f, XFCurve curve)
{
  if (curve == XFCurve::POWER)
    return sqrtf (f);
  else
    return f;
}

float
Voice::xfin_gain (int value, int lo, int hi, XFCurve curve)
{
  if (value < lo)
    return 0;
  if (value < hi && hi > lo)
    return apply_xfcurve (float (value - lo) / (hi - lo), curve);
  else
    return 1;
}

float
Voice::xfout_gain (int value, int lo, int hi, XFCurve curve)
{
  if (value > hi)
    return 0;
  if (value > lo && hi > lo)
    return apply_xfcurve (1 - float (value - lo) / (hi - lo), curve);
  else
    return 1;
}

void
Voice::update_volume_gain()
{
  float volume = region_->volume;
  volume += synth_->get_cc_vec_value (channel_, region_->gain_cc);

  volume_gain_ = db_to_factor (volume);

  volume_gain_ *= amp_random_gain_;
  volume_gain_ *= xfin_gain (velocity_, region_->xfin_lovel, region_->xfin_hivel, region_->xf_velcurve);
  volume_gain_ *= xfout_gain (velocity_, region_->xfout_lovel, region_->xfout_hivel, region_->xf_velcurve);

  volume_gain_ *= xfin_gain (key_, region_->xfin_lokey, region_->xfin_hikey, region_->xf_keycurve);
  volume_gain_ *= xfout_gain (key_, region_->xfout_lokey, region_->xfout_hikey, region_->xf_keycurve);

  // xfin_locc / xfin_hicc
  for (auto xfcc : region_->xfin_ccs)
    volume_gain_ *= xfin_gain (synth_->get_cc (channel_, xfcc.cc), xfcc.lo, xfcc.hi, region_->xf_cccurve);

  // xfout_locc / xfout_hicc
  for (auto xfcc : region_->xfout_ccs)
    volume_gain_ *= xfout_gain (synth_->get_cc (channel_, xfcc.cc), xfcc.lo, xfcc.hi, region_->xf_cccurve);
}

void
Voice::update_amplitude_gain()
{
  float gain = region_->amplitude * 0.01f;

  /* for CCs modulating amplitude, the amplitudes of the individual CCs are multiplied (not added) */
  for (const auto& entry : region_->amplitude_cc)
    gain *= synth_->get_cc_curve (channel_, entry) * entry.value * 0.01f;

  amplitude_gain_ = gain;
}

void
Voice::update_cutoff (FImpl& fi, bool now)
{
  float delta_cent = synth_->get_cc_vec_value (channel_, fi.params->cutoff_cc);

  // key tracking
  delta_cent += (key_ - fi.params->keycenter) * fi.params->keytrack;

  // velocity tracking
  delta_cent += velocity_ * (1 / 127.f) * fi.params->veltrack;

  /* FIXME: maybe smooth only cc value */
  fi.cutoff_smooth.set (fi.params->cutoff * exp2f (delta_cent * (1 / 1200.f)), now);
}

void
Voice::update_resonance (FImpl& fi, bool now)
{
  fi.resonance_smooth.set (fi.params->resonance + synth_->get_cc_vec_value (channel_, fi.params->resonance_cc), now);
}

void
Voice::stop (OffMode off_mode)
{
  state_ = Voice::RELEASED;
  envelope_.stop (off_mode);
  filter_envelope_.stop (OffMode::NORMAL);
}

void
Voice::kill()
{
  state_ = Voice::IDLE;
}

void
Voice::update_cc (int controller)
{
  if (region_->xfin_ccs.size() || region_->xfout_ccs.size())
    {
      update_volume_gain();
      update_lr_gain (false);
    }
  if (region_->pan_cc.contains (controller))
    {
      update_pan_gain();
      update_lr_gain (false);
    }
  if (region_->gain_cc.contains (controller))
    {
      update_volume_gain();
      update_lr_gain (false);
    }
  if (region_->amplitude_cc.contains (controller))
    {
      update_amplitude_gain();
      update_lr_gain (false);
    }

  if (region_->tune_cc.contains (controller))
    update_replay_speed (false);

  auto update_filter = [controller, this] (FImpl& fi)
    {
      if (fi.params->cutoff_cc.contains (controller))
        update_cutoff (fi, false);

      if (fi.params->resonance_cc.contains (controller))
        update_resonance (fi, false);
    };
  update_filter (fimpl_);
  update_filter (fimpl2_);
}

void
Voice::update_gain()
{
  update_lr_gain (false);
}

void
Voice::set_pitch_bend (int bend)
{
  pitch_bend_value_ = bend / double (0x2000) - 1.0;
}

void
Voice::update_pitch_bend (int bend)
{
  set_pitch_bend (bend);
  update_replay_speed (false);
}

void
Voice::process (float **orig_outputs, uint orig_n_frames)
{
  const auto csample = region_->cached_sample;
  const auto channels = csample->channels;

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

              if (loop_enabled_)
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

              if (loop_enabled_)
                if (x > uint (region_->loop_end * 2))
                  x = region_->loop_start * 2;
            }
        }
    };
  /* delay start of voice for delay_samples_ frames */
  uint dframes = std::min (orig_n_frames, delay_samples_);
  delay_samples_ -= dframes;

  /* compute n_frames / outputs according to delay */
  uint n_frames = orig_n_frames - dframes;
  float *outputs[2] =
    {
      orig_outputs[0] + dframes,
      orig_outputs[1] + dframes
    };

  float out_l[n_frames];
  float out_r[n_frames];

  /* render voice */
  for (uint i = 0; i < n_frames; i++)
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
              out_l[i] = interp * amp_gain;
              out_r[i] = interp * amp_gain;
            }
          else if (channels == 2)
            {
              std::array<float, 4> fsamples;
              get_samples_stereo (x, fsamples);

              out_l[i] = (fsamples[0] * (1 - frac) + fsamples[2] * frac) * amp_gain;
              out_r[i] = (fsamples[1] * (1 - frac) + fsamples[3] * frac) * amp_gain;
            }
          else
            {
              assert (false);
            }
        }
      else
        {
          state_ = IDLE;

          /* output memory is uninitialized, so we need to explicitely write every sampl when done */
          out_l[i] = 0;
          out_r[i] = 0;
        }
      ppos_ += replay_speed_.get_next();
      if (loop_enabled_)
        while (ppos_ > region_->loop_end)
          ppos_ -= (region_->loop_end - region_->loop_start);
    }

  /* process filters */
  process_filter (fimpl_, true, out_l, out_r, n_frames);
  process_filter (fimpl2_, false, out_l, out_r, n_frames);

  /* add samples to output buffer */
  if (channels == 2)
    {
      for (uint i = 0; i < n_frames; i++)
        {
          outputs[0][i] += out_l[i] * left_gain_.get_next();
          outputs[1][i] += out_r[i] * right_gain_.get_next();
        }
    }
  else
    {
      for (uint i = 0; i < n_frames; i++)
        {
          outputs[0][i] += out_l[i] * left_gain_.get_next();
          outputs[1][i] += out_l[i] * right_gain_.get_next();
        }
    }
}

void
Voice::process_filter (FImpl& fi, bool envelope, float *left, float *right, uint n_frames)
{
  if (fi.params->type == Filter::Type::NONE)
    return;

  float mod_cutoff[n_frames];
  float mod_resonance[n_frames];
  float mod_env[n_frames];

  auto run_filter = [&] (const auto& cr_func)
    {
      const auto csample = region_->cached_sample;
      const auto channels = csample->channels;

      if (channels == 2)
        fi.filter.process_mod (left, right, cr_func, n_frames);
      else
        fi.filter.process_mod_mono (left, cr_func, n_frames);
    };

  if (envelope)
    {
      const float depth_factor = filter_envelope_depth_ / 1200.;

      if (fi.cutoff_smooth.is_constant() && filter_envelope_.is_constant() && fi.resonance_smooth.is_constant())
        {
          float cutoff = fi.cutoff_smooth.get_next() * exp2f (filter_envelope_.get_next() * depth_factor);
          float resonance = fi.resonance_smooth.get_next();

          run_filter ([&] (int i)
            {
              return Filter::CR (cutoff, resonance);
            });
        }
      else
        {
          for (uint i = 0; i < n_frames; i++)
            {
              mod_cutoff[i]    = fi.cutoff_smooth.get_next();
              mod_env[i]       = filter_envelope_.get_next();
              mod_resonance[i] = fi.resonance_smooth.get_next();
            }

          run_filter ([&] (int i)
            {
              return Filter::CR (mod_cutoff[i] * exp2f (mod_env[i] * depth_factor), mod_resonance[i]);
            });
        }
    }
  else
    {
      if (fi.cutoff_smooth.is_constant() && fi.resonance_smooth.is_constant())
        {
          float cutoff    = fi.cutoff_smooth.get_next();
          float resonance = fi.resonance_smooth.get_next();

          run_filter ([&] (int i)
            {
              return Filter::CR (cutoff, resonance);
            });
        }
      else
        {
          for (uint i = 0; i < n_frames; i++)
            {
              mod_cutoff[i]    = fi.cutoff_smooth.get_next();
              mod_resonance[i] = fi.resonance_smooth.get_next();
            }
          run_filter ([&] (int i)
            {
              return Filter::CR (mod_cutoff[i], mod_resonance[i]);
            });
        }
    }
}
