// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <math.h>

#include <array>

#include "voice.hh"
#include "synth.hh"
#include "upsample.hh"

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

  replay_speed_.set (exp2f (semi_tones / 12) * region_->cached_sample->sample_rate() / sample_rate_, now);
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
  width_factor_.reset (sample_rate, 0.020);

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

  quality_ = synth_->sample_quality();
  int upsample = quality_ == 3 ? 2 : 1; // upsample for best quality interpolator

  /* play start position */
  uint offset = region.offset;
  offset += lrint (region.offset_random * synth_->normalized_random_value());
  offset += lrint (synth_->get_cc_vec_value (channel_, region.offset_cc));
  ppos_ = offset * upsample;
  if (ppos_ > region.loop_end * upsample)
    loop_enabled_ = false;
  last_ippos_ = 0;

  update_volume_gain();
  update_amplitude_gain();
  update_pan_gain();
  update_cc7_cc10_gain();
  update_lr_gain (true);
  update_width_factor (true);

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

  play_handle_.start_playback (region.cached_sample.get(), synth_->live_mode());
  sample_reader_.restart (&play_handle_, region.cached_sample.get(), upsample);
  if (loop_enabled_)
    sample_reader_.set_loop (region.loop_start, region.loop_end);

  synth_->debug ("location %s\n", region.location.c_str());
  synth_->debug ("new voice %s - channels %d\n", region.sample.c_str(), region.cached_sample->channels());

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

  lfo_gen_.start (region, channel_, sample_rate_);
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
Voice::update_cc7_cc10_gain()
{
  double gain = 1;
  if (region_->volume_cc7)
    gain = synth_->get_curve_value (4, synth_->get_cc (channel_, 7));

  float pan = 0;
  if (region_->pan_cc10)
    {
      pan = 100 * synth_->get_curve_value (1, synth_->get_cc (channel_, 10));
      pan = clamp (pan, -100.f, 100.f);
    }

  /* for center panning, pan_stereo_factor returns 1/sqrt(2) for left and
   * right gain, but we want a factor of 1 for both channels in this case
   */
  gain *= M_SQRT2;
  cc7_cc10_left_gain_ = gain * pan_stereo_factor (pan, 0);
  cc7_cc10_right_gain_ = gain * pan_stereo_factor (pan, 1);
}

void
Voice::update_lr_gain (bool now)
{
  const float global_gain = synth_->gain() * volume_gain_ * velocity_gain_ * rt_decay_gain_ * amplitude_gain_;

  synth_->debug (" - gain l=%.2f r=%.2f\n", 32768 * pan_left_gain_ * global_gain, 32768 * pan_right_gain_ * global_gain);
  left_gain_.set (cc7_cc10_left_gain_ * pan_left_gain_ * global_gain, now);
  right_gain_.set (cc7_cc10_right_gain_* pan_right_gain_ * global_gain, now);
}

void
Voice::update_width_factor (bool now)
{
  float width = region_->width + synth_->get_cc_vec_value (channel_, region_->width_cc);
  width_factor_.set ((width + 100.f) * 0.01f * 0.5f, now);
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
  float volume = region_->volume + region_->group_volume + region_->master_volume + region_->global_volume;
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

  if (region_->loop_mode == LoopMode::SUSTAIN)
    sample_reader_.stop_loop();
}

void
Voice::kill()
{
  if (state_ != Voice::IDLE)
    {
      state_ = Voice::IDLE;
      play_handle_.end_playback();

      synth_->idle_voices_changed();
    }
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
  if (controller == 7 || controller == 10)
    {
      update_cc7_cc10_gain();
      update_lr_gain (false);
    }

  if (region_->tune_cc.contains (controller))
    update_replay_speed (false);
  if (region_->width_cc.contains (controller))
    update_width_factor (false);

  auto update_filter = [controller, this] (FImpl& fi)
    {
      if (fi.params->cutoff_cc.contains (controller))
        update_cutoff (fi, false);

      if (fi.params->resonance_cc.contains (controller))
        update_resonance (fi, false);
    };
  update_filter (fimpl_);
  update_filter (fimpl2_);
  lfo_gen_.update_ccs();
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
Voice::process (float **outputs, uint n_frames)
{
  int channels = region_->cached_sample->channels();

  if (quality_ == 1)
    {
      if (channels == 1)
        process_impl<1, 1> (outputs, n_frames);
      else
        process_impl<1, 2> (outputs, n_frames);
    }
  else if (quality_ == 2)
    {
      if (channels == 1)
        process_impl<2, 1> (outputs, n_frames);
      else
        process_impl<2, 2> (outputs, n_frames);
    }
  else if (quality_ == 3)
    {
      if (channels == 1)
        process_impl<3, 1> (outputs, n_frames);
      else
        process_impl<3, 2> (outputs, n_frames);
    }
}

/*----- interpolation helpers -----*/
// from: Polynomial Interpolators for High-Quality Resampling of Oversampled Audio
// by Olli Niemitalo in October 2001
static inline float
interp_optimal_2x_4p (float ym1, float y0, float y1, float y2, float x)
{
  // Optimal 2x (4-point, 4th-order) (z-form)
  float z = x - 0.5f;
  float even1 = y1+y0, odd1 = y1-y0;
  float even2 = y2+ym1, odd2 = y2-ym1;
  float c0 = even1*0.45645918406487612f + even2*0.04354173901996461f;
  float c1 = odd1*0.47236675362442071f + odd2*0.17686613581136501f;
  float c2 = even1*-0.253674794204558521f + even2*0.25371918651882464f;
  float c3 = odd1*-0.37917091811631082f + odd2*0.11952965967158000f;
  float c4 = even1*0.04252164479749607f + even2*-0.04289144034653719f;
  return (((c4*z+c3)*z+c2)*z+c1)*z+c0;
}

static inline float
interp_hermite_6p3o (float ym2, float ym1, float y0, float y1, float y2, float y3, float x)
{
  // 6-point, 3rd-order Hermite (x-form)
  float c1 = ym2 - y2 + 8 * (y1 - ym1);
  float c2 = 15 * ym1 - 28 * y0 + 20 * y1 - 6 * y2 + y3 - 2 * ym2;
  float c3 = ym2 - y3 + 7 * (y2 - ym1) + 16 * (y0 - y1);
  return (((c3*x+c2)*x+c1)*x) * (1/12.0f) + y0;
}

template<int QUALITY, int CHANNELS>
void
Voice::process_impl (float **orig_outputs, uint orig_n_frames)
{
  static_assert (QUALITY >= 1 || QUALITY <= 3);
  static_assert (CHANNELS == 1 || CHANNELS == 2);
  constexpr int UPSAMPLE = QUALITY == 3 ? 2 : 1;

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

  /* render lfos */
  float lfo_buffer[LFOGen::MAX_OUTPUTS * Synth::MAX_BLOCK_SIZE];
  if (lfo_gen_.need_process())
    lfo_gen_.process (lfo_buffer, n_frames);

  const float *lfo_pitch = lfo_gen_.get (LFOGen::PITCH);
  if (!lfo_pitch)
    lfo_pitch = synth_->const_block_1();

  /* render voice */
  float out_l[Synth::MAX_BLOCK_SIZE];
  float out_r[Synth::MAX_BLOCK_SIZE];

  for (uint i = 0; i < n_frames; i++)
    {
      if (!sample_reader_.done() && !envelope_.done())
        {
          const int64_t ippos = ppos_;
          const int delta_pos = ippos - last_ippos_;
          const float frac = ppos_ - ippos;
          last_ippos_ = ippos;

          ppos_ += replay_speed_.get_next() * lfo_pitch[i] * UPSAMPLE;

          const float amp_gain = envelope_.get_next();
          if (CHANNELS == 1)
            {
              if constexpr (QUALITY == 1)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 2> (delta_pos);

                  out_l[i] = (samples[0] + frac * (samples[1] - samples[0])) * amp_gain;
                }
              if constexpr (QUALITY == 2)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 6> (delta_pos);

                  out_l[i] = interp_hermite_6p3o (samples[0], samples[1], samples[2], samples[3], samples[4], samples[5], frac) * amp_gain;
                }
              if constexpr (QUALITY == 3)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 4> (delta_pos);

                  out_l[i] = interp_optimal_2x_4p (samples[0], samples[1], samples[2], samples[3], frac) * amp_gain;
                }
            }
          else if (CHANNELS == 2)
            {
              if constexpr (QUALITY == 1)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 2> (delta_pos);

                  out_l[i] = (samples[0] + frac * (samples[2] - samples[0])) * amp_gain;
                  out_r[i] = (samples[1] + frac * (samples[3] - samples[1])) * amp_gain;
                }
              if constexpr (QUALITY == 2)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 6> (delta_pos);

                  out_l[i] = interp_hermite_6p3o (samples[0], samples[2], samples[4], samples[6], samples[8], samples[10], frac) * amp_gain;
                  out_r[i] = interp_hermite_6p3o (samples[1], samples[3], samples[5], samples[7], samples[9], samples[11], frac) * amp_gain;
                }
              if constexpr (QUALITY == 3)
                {
                  const float *samples = sample_reader_.skip<UPSAMPLE, CHANNELS, 4> (delta_pos);

                  out_l[i] = interp_optimal_2x_4p (samples[0], samples[2], samples[4], samples[6], frac) * amp_gain;
                  out_r[i] = interp_optimal_2x_4p (samples[1], samples[3], samples[5], samples[7], frac) * amp_gain;
                }
            }
          else
            {
              assert (false);
            }
        }
      else
        {
          kill();

          /* output memory is uninitialized, so we need to explicitely write every sampl when done */
          out_l[i] = 0;
          out_r[i] = 0;
        }
    }

  /* process filters */
  if (fimpl_.params->type != Filter::Type::NONE)
    process_filter (fimpl_, true, out_l, out_r, n_frames, lfo_gen_.get (LFOGen::CUTOFF));

  if (fimpl2_.params->type != Filter::Type::NONE)
    process_filter (fimpl2_, false, out_l, out_r, n_frames, nullptr);

  /* process width */
  if (CHANNELS == 2)
    process_width (out_l, out_r, n_frames);

  /* add samples to output buffer */
  const float *lfo_volume = lfo_gen_.get (LFOGen::VOLUME);
  const bool   const_gain = (!lfo_volume && left_gain_.is_constant() && right_gain_.is_constant());
  if (!lfo_volume)
    lfo_volume = synth_->const_block_1();

  if (CHANNELS == 2)
    {
      if (const_gain)
        {
          const float gain_left = left_gain_.get_next();
          const float gain_right = right_gain_.get_next();

           for (uint i = 0; i < n_frames; i++)
             {
               outputs[0][i] += out_l[i] * gain_left;
               outputs[1][i] += out_r[i] * gain_right;
             }
        }
      else
        {
          for (uint i = 0; i < n_frames; i++)
            {
              outputs[0][i] += out_l[i] * lfo_volume[i] * left_gain_.get_next();
              outputs[1][i] += out_r[i] * lfo_volume[i] * right_gain_.get_next();
            }
        }
    }
  else
    {
      if (const_gain)
        {
          const float gain_left = left_gain_.get_next();
          const float gain_right = right_gain_.get_next();

          for (uint i = 0; i < n_frames; i++)
            {
              outputs[0][i] += out_l[i] * gain_left;
              outputs[1][i] += out_l[i] * gain_right;
            }
        }
      else
        {
          for (uint i = 0; i < n_frames; i++)
            {
              outputs[0][i] += out_l[i] * lfo_volume[i] * left_gain_.get_next();
              outputs[1][i] += out_l[i] * lfo_volume[i] * right_gain_.get_next();
            }
        }
    }
}

void
Voice::process_filter (FImpl& fi, bool envelope, float *left, float *right, uint n_frames, const float *lfo_cutoff_factor)
{
  float mod_cutoff[Synth::MAX_BLOCK_SIZE];
  float mod_resonance[Synth::MAX_BLOCK_SIZE];
  float mod_env[Synth::MAX_BLOCK_SIZE];

  auto run_filter = [&] (const auto& cr_func)
    {
      int channels = region_->cached_sample->channels();

      if (channels == 2)
        fi.filter.process_mod (left, right, cr_func, n_frames);
      else
        fi.filter.process_mod_mono (left, cr_func, n_frames);
    };

  if (envelope && filter_envelope_depth_)
    {
      const float depth_factor = filter_envelope_depth_ / 1200.;

      if (fi.cutoff_smooth.is_constant() && filter_envelope_.is_constant() && fi.resonance_smooth.is_constant() && !lfo_cutoff_factor)
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
              float cutoff = mod_cutoff[i] * exp2f (mod_env[i] * depth_factor);
              if (lfo_cutoff_factor)
                cutoff *= lfo_cutoff_factor[i];

              return Filter::CR (cutoff, mod_resonance[i]);
            });
        }
    }
  else
    {
      if (fi.cutoff_smooth.is_constant() && fi.resonance_smooth.is_constant() && !lfo_cutoff_factor)
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
              float cutoff = mod_cutoff[i];
              if (lfo_cutoff_factor)
                cutoff *= lfo_cutoff_factor[i];

              return Filter::CR (cutoff, mod_resonance[i]);
            });
        }
    }
}

void
Voice::process_width (float *out_l, float *out_r, uint n_frames)
{
  auto apply_width = [&] (uint i, const float width_factor)
    {
      const float left = out_l[i];
      const float right = out_r[i];
      const float a = width_factor;    // factor for same channel
      const float b = 1.f - a;         // factor for other channel
      out_l[i] = a * left + b * right;
      out_r[i] = b * left + a * right;
    };

  if (width_factor_.is_constant())
    {
      const float width_factor = width_factor_.get_next();

      // if width factor is 1, width is 100% and no processing is needed
      if (fabs (width_factor - 1) > 1e-6)
        {
          // compiler should auto-vectorize this loop
          for (uint i = 0; i < n_frames; i++)
            apply_width (i, width_factor);
        }
    }
  else
    {
      for (uint i = 0; i < n_frames; i++)
        apply_width (i, width_factor_.get_next());
    }
}

template<int UPSAMPLE, int CHANNELS, int INTERP_POINTS>
inline const float *
SampleReader::skip (int delta)
{
  relative_pos_ += delta;

  bool in_loop = false;
  if (loop_start_ >= 0)
    {
      // FIXME: may want to have more states:
      //  (a) entered loop
      //  (b) reached end of loop (n times) to implement loop_count opcode
      in_loop = (relative_pos_ >= loop_start_ * UPSAMPLE);

      while (relative_pos_ > loop_end_ * UPSAMPLE)
        {
          relative_pos_ -= (loop_end_ - loop_start_ + 1) * UPSAMPLE;
        }
    }

  static_assert (UPSAMPLE == 1 || UPSAMPLE == 2);
  static_assert (INTERP_POINTS % 2 == 0);

  auto close_to_loop_point = [&] (int n)
    {
      return in_loop && ((relative_pos_ / UPSAMPLE - loop_start_ < n) || (loop_end_ - relative_pos_ / UPSAMPLE < n));
    };
  if constexpr (UPSAMPLE == 1)
    {
      const int start_x = relative_pos_ - (INTERP_POINTS - 2) / 2;
      const float *samples = nullptr;

      if (!close_to_loop_point (INTERP_POINTS))
        samples = play_handle_->get_n (start_x * CHANNELS, INTERP_POINTS * CHANNELS);

      if (samples)
        {
          return samples;
        }
      else
        {
          for (int i = 0; i < INTERP_POINTS; i++)
            {
              int x = start_x + i;
              if (in_loop)
                {
                  while (x < loop_start_)
                    x += loop_end_ - loop_start_ + 1;
                  while (x > loop_end_)
                    x -= loop_end_ - loop_start_ + 1;
                }
              for (int c = 0; c < CHANNELS; c++)
                {
                  samples_[i * CHANNELS + c] = play_handle_->get (x * CHANNELS + c);
                }
            }

          return &samples_[0];
        }
    }
  if constexpr (UPSAMPLE == 2)
    {
      static_assert (INTERP_POINTS == 4);

      const int N = 24;
      const float *input = nullptr;

      if (!close_to_loop_point (N))
        {
          const int start_x = (relative_pos_ / 2 * CHANNELS) - N * CHANNELS;
          input = play_handle_->get_n (start_x, N * 2 * CHANNELS);
        }

      float input_stack[N * 2 * CHANNELS];
      if (!input)
        {
          for (int n = 0; n < N * 2; n++)
            {
              int x = (relative_pos_ / 2) + n - N;

              if (in_loop)
                {
                  while (x > loop_end_)
                    x -= (loop_end_ - loop_start_ + 1);
                  while (x < loop_start_)
                    x += (loop_end_ - loop_start_ + 1);
                }
              for (int c = 0; c < CHANNELS; c++)
                {
                  input_stack[n * CHANNELS + c] = play_handle_->get (x * CHANNELS + c);
                }
            }
          input = input_stack;
        }
      input += N * CHANNELS;

      int diff = relative_pos_ / 2 - last_index_;
      if (diff >= 0 && diff < upsample_buffer_size_ - 2)
        {
          // samples are already in upsample buffer
        }
      else
        {
          last_index_ = relative_pos_ / 2;

          int i = upsample_buffer_size_ - diff;
          if (i > 0 && i < 3)
            {
              /* we are at the end of the upsample buffer and there is some overlap
               *
               * typically this happens if we read the sample slowly, so it makes
               * sense to copy the overlapping part and prefill the whole buffer
               */
              std::copy_n (&samples_[2 * diff * CHANNELS], 2 * i * CHANNELS, &samples_[0]);
              upsample_buffer_size_ = MAX_UPSAMPLE_BUFFER_SIZE;
            }
          else
            {
              /* a jump occured
               *
               * only upsample 3 samples here, because after a jump it is
               * likely that another jump occurs (for fast forward reading), in
               * which case preparing additional samples won't help
               */
              i = 0;
              upsample_buffer_size_ = 3;
            }
          while (i < upsample_buffer_size_)
            {
              upsample<CHANNELS> (&input[(i - 1) * CHANNELS], &samples_[2 * CHANNELS * i]);
              i++;
            }
          diff = 0;
        }

      return &samples_[((relative_pos_ & 1) + 1 + diff * 2) * CHANNELS];
    }
}
