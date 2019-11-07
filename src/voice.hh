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

struct Voice
{
  int sample_rate_ = 44100;
  int channel = 0;
  int key = 0;
  int velocity = 0;
  bool used = false;
  double ppos = 0;
  float left_gain = 0;
  float right_gain = 0;
  uint64_t start_frame_count = 0;
  Trigger trigger = Trigger::ATTACK;
  Envelope envelope;

  const Region *region = nullptr;

  void process (float **outputs, uint nframes);
};

}

#endif /* LIQUIDSFZ_VOICE_HH */
