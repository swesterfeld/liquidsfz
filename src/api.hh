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

#ifndef LIQUIDSFZ_LIQUIDSFZ_HH
#define LIQUIDSFZ_LIQUIDSFZ_HH

#include <memory>

namespace LiquidSFZ
{

class Synth
{
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  Synth();
  ~Synth();

  /**
   * \brief Set sample rate of the synthesizer
   *
   * @param sample_rate   new sample rate
   */
  void set_sample_rate (uint sample_rate);

  /**
   * \brief Load .sfz file including all samples
   *
   * @param filename   name of the .sfz file
   *
   * @returns true if the .sfz could be loaded successfully
   */
  bool load (const std::string& filename);

  /**
   * \brief Add a new midi event to the event list for the next process() call
   *
   * Events must be added in the correct order, sorted by offset. The events
   * will be processed during the next process() call.
   *
   * @param offset      time index for the midi event
   * @param midi_data   raw midi data (for instance {0x90, 60, 100} for note on, middle C, velocity 100)
   */
  void add_midi_event (uint offset, unsigned char *midi_data);

  /**
   * \brief Synthesize audio
   *
   * This will render one stereo output stream, left output to outputs[0] and
   * right output to outputs[1]. The contents of the output buffers will be overwritten.
   * The midi events added by add_midi_event() will be processed at the appropriate time
   * offsets. After process() is done, the contents  of midi buffer will be discarded.
   *
   * @param outputs    buffers for the output of the synthesizer
   * @param n_frames   number of stereo frames to be written
   */
  void process (float **outputs, uint nframes);
};

}

/**
 * \mainpage LiquidSFZ
 *
 * \section intro_sec Introduction
 *
 * LiquidSFZ is a library to load and replay sample based instruments in .sfz format. If you want
 * to use libquidsfz in your own project, use the LiquidSFZ::Synth class.
 */

#endif
