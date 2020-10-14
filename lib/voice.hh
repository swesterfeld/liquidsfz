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

#include "envelope.hh"

#ifndef LIQUIDSFZ_VOICE_HH
#define LIQUIDSFZ_VOICE_HH

namespace LiquidSFZInternal
{

class Voice
{
  LinearSmooth left_gain_;
  LinearSmooth right_gain_;

  float volume_gain_ = 0;
  float amplitude_gain_ = 0;
  float velocity_gain_ = 0;
  float rt_decay_gain_ = 0;
  float pan_left_gain_ = 0;
  float pan_right_gain_ = 0;

  float amp_random_gain_ = 0;
  float pitch_random_cent_ = 0;
  uint  delay_samples_ = 0;

  void update_volume_gain();
  void update_amplitude_gain();
  void update_pan_gain();
  void update_lr_gain (bool now);

  float amp_value (float vnorm, const AmpParam& amp_param);

  LinearSmooth replay_speed_;
  float        pitch_bend_value_ = 0; // [-1:1]

  void set_pitch_bend (int value);
  void update_replay_speed (bool now);
public:
  Synth *synth_;
  int sample_rate_ = 44100;
  int channel_ = 0;
  int key_ = 0;
  int velocity_ = 0;
  bool loop_enabled_ = false;

  enum State {
    ACTIVE,
    SUSTAIN,
    RELEASED,
    IDLE
  };
  State state_ = IDLE;

  double ppos_ = 0;
  uint64_t start_frame_count_ = 0;
  Trigger trigger_ = Trigger::ATTACK;
  Envelope envelope_;

  const Region *region_ = nullptr;

  Voice (Synth *synth) :
    synth_ (synth)
  {
  }
  double pan_stereo_factor (double region_pan, int ch);
  double velocity_track_factor (const Region& r, int midi_velocity);

  void start (const Region& region, int channel, int key, int velocity, double time_since_note_on, uint64_t global_frame_count, uint sample_rate);
  void stop (OffMode off_mode);
  void kill();
  void process (float **outputs, uint nframes);
  uint off_by();
  void update_cc (int controller);
  void update_gain();

  void update_pitch_bend (int bend);

  float xfin_gain (int value, int lo, int hi, XFCurve curve);
  float xfout_gain (int value, int lo, int hi, XFCurve curve);
  float apply_xfcurve (float f, XFCurve curve);
};

}

#endif /* LIQUIDSFZ_VOICE_HH */
