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
#include <functional>

/** \file liquidsfz.hh
 * \brief This header contains the public API for liquidsfz
 */

namespace LiquidSFZ
{

/**
 * \brief Log levels for @ref LiquidSFZ::Synth
 */
enum class Log {
  DEBUG,
  INFO,
  WARNING,
  ERROR,
  DISABLE_ALL // special log level which can be used to disable all logging
};

class CCInfo
{
  friend class Synth;

  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  CCInfo();
  CCInfo (CCInfo&&) = default;
  ~CCInfo();

  int cc() const;
  std::string label() const;
  bool has_label() const;
  int default_value() const;
};

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
   * \brief Set maximum number of voices
   *
   * @param n_voices   maximum number of voices
   */
  void set_max_voices (uint n_voices);

  /**
   * \brief Set global gain
   *
   * @param gain  gain (as a factor)
   */
  void set_gain (float gain);

  /**
   * \brief Load .sfz file including all samples
   *
   * @param filename   name of the .sfz file
   *
   * @returns true if the .sfz could be loaded successfully
   */
  bool load (const std::string& filename);

  std::vector<CCInfo> list_ccs() const;

  /**
   * \brief Add a note on event
   *
   * This function adds a note on event to the event list that will be rendered
   * by the next process() call. A note on with velocity 0 will be treated as
   * note off.
   *
   * Events must always be added sorted by event time (time_frames).
   *
   * @param time_frames the start time of the event (in frames)
   * @param channel     midi channel
   * @param key         note that was pressed
   * @param velocity    velocity for note on
   */
  void add_event_note_on (uint time_frames, int channel, int key, int velocity);

  /**
   * \brief Add a note off event
   *
   * This function adds a note off event to the event list that will be rendered
   * by the next process() call.
   *
   * Events must always be added sorted by event time (time_frames).
   *
   * @param time_frames the start time of the event (in frames)
   * @param channel     midi channel
   * @param key         note that was pressed
   */
  void add_event_note_off (uint time_frames, int channel, int key);

  /**
   * \brief Add a cc event
   *
   * This function adds a cc event to the event list that will be rendered
   * by the next process() call.
   *
   * Events must always be added sorted by event time (time_frames).
   *
   * @param time_frames the start time of the event (in frames)
   * @param channel     midi channel
   * @param cc          the controller
   * @param value       new value for the controller
   */
  void add_event_cc (uint time_frames, int channel, int cc, int value);

  /**
   * \brief Synthesize audio
   *
   * This will render one stereo output stream, left output to outputs[0] and
   * right output to outputs[1]. The contents of the output buffers will be overwritten.
   *
   * Events that should be processed by this function can be added by calling some
   * event functions before process():
   *  - add_event_note_on()
   *  - add_event_note_off()
   *  - add_event_cc()
   *
   * These events will processed at the appropriate time positions (which means
   * we provide sample accurate timing for all events). Events must always be
   * added sorted by event time. After process() is done, the contents of event
   * buffer will be discarded.
   *
   * @param outputs    buffers for the output of the synthesizer
   * @param n_frames   number of stereo frames to be written
   */
  void process (float **outputs, uint n_frames);

  /**
   * \brief Set minimum log level
   *
   * This function can be used to set the minimum level of log messages that you
   * are interested in. By default this is set to Log::INFO, which means that you
   * will get errors, warnings and info messages, but not debug messages. Note
   * that if you want to ignore some messages, it is better (more efficient) to
   * do so by setting the log level than by setting a logging function with
   * set_log_function() and ignoring some messages.
   *
   * If you want to disable all messages, log_level can be set to Log::DISABLE_ALL.
   *
   * @param log_level minimum log level
   */
  void set_log_level (Log log_level);

  /**
   * \brief Set custom logging function
   *
   * @param log_function function to be called for log entries
   */
  void set_log_function (std::function<void (Log, const char *)> log_function);

  /**
   * \brief Set progress function
   *
   * Loading .sfz files can take a long time. In order to give feedback to the
   * user, a progress function can be registered before calling load().  This
   * progress function will be called during load() periodically with a
   * percentage from 0%..100%. This can be used to display a progress bar at
   * the ui.
   *
   * @param progress_function function to be called
   */
  void set_progress_function (std::function<void (double)> progress_function);
};

}

/**
 *
\mainpage LiquidSFZ

\section intro_sec Introduction

LiquidSFZ is a library to load and replay sample based instruments in .sfz format. If you want
to use libquidsfz in your own project, use the LiquidSFZ::Synth class.

\section simple_example A Simple Example

\code
// Compile this example with:
// $ g++ -o example example.cc `pkg-config --libs --cflags liquidsfz`

#include <liquidsfz.hh>

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      printf ("usage: %s <sfz_filename>\n", argv[0]);
      return 1;
    }

  LiquidSFZ::Synth synth;
  synth.set_sample_rate (48000);

  if (!synth.load (argv[1])) // load sfz
    {
      printf ("%s: failed to load sfz file '%s'\n", argv[0], argv[1]);
      return 1;
    }

  // start a note
  synth.add_event_note_on (0, 0, 60, 100);

  // render one block of audio data
  float left_output[1024];
  float right_output[1024];
  float *output[2] = { left_output, right_output };
  synth.process (output, 1024);

  // at this point we would typically play back the audio data
  for (int i = 0; i < 1024; i++)
    printf ("%d %f %f\n", i, left_output[i], right_output[i]);
}
\endcode
*/

#endif
