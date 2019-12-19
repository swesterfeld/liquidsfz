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
public:
  Synth *synth_;
  int sample_rate_ = 44100;
  int channel_ = 0;
  int key_ = 0;
  int velocity_ = 0;

  enum State {
    ACTIVE,
    SUSTAIN,
    RELEASED,
    IDLE
  };
  State state_ = IDLE;

  double ppos_ = 0;
  float left_gain_ = 0;
  float right_gain_ = 0;
  uint64_t start_frame_count_ = 0;
  Trigger trigger_ = Trigger::ATTACK;
  Envelope envelope_;

  const Region *region_ = nullptr;

  Voice (Synth *synth) :
    synth_ (synth)
  {
  }
  double pan_stereo_factor (const Region& r, int ch);
  double velocity_track_factor (const Region& r, int midi_velocity);
  double replay_speed();

  void start (const Region& region, int channel, int key, int velocity, double time_since_note_on, uint64_t global_frame_count, uint sample_rate);
  void stop (OffMode off_mode);
  void process (float **outputs, uint nframes);
  uint off_by();
};

}

#endif /* LIQUIDSFZ_VOICE_HH */
