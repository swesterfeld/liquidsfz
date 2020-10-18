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

/**
 * \brief Information for one continuous controller
 */
class CCInfo
{
  friend class Synth;

  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  CCInfo();
  CCInfo (CCInfo&&) = default;
  ~CCInfo();

  /**
   * @returns CC controller number (0-127)
   */
  int cc() const;

  /**
   * Some .sfz files contain labels (label_cc opcode) for the CCs they provide.
   * If a label is available, this function will return it.  If no label was
   * defined in the .sfz file, a label will be generated automatically (like
   * "CC080"). To check if a label is available, call @ref has_label().
   *
   * @returns label for the CC controller
   */
  std::string label() const;

  /**
   * This function returns true if the .sfz file contains a label for
   * this CC (that is, if a label_cc opcode was found for this CC).
   *
   * @returns true if a label is available.
   */
  bool has_label() const;

  /**
   * This contains the initial value for the CC.
   *
   * @returns the a CC value (0-127)
   */
  int default_value() const;
};

/**
 * \brief SFZ Synthesizer (main API)
 */
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
   * \brief Get active voice count
   *
   * The maximum number of voices can be set using \ref set_max_voices.
   * Note that the number of voices that are active is not the same as
   * the number of midi notes, as synthesizing one note can require more
   * than one voice, for instance if multiple samples are cross-faded.
   *
   * @returns the number of currently active voices
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  uint active_voice_count() const;

  /**
   * \brief Set global gain
   *
   * @param gain  gain (as a factor)
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void set_gain (float gain);

  /**
   * \brief Load .sfz file including all samples
   *
   * @param filename   name of the .sfz file
   *
   * @returns true if the .sfz could be loaded successfully
   *
   * Typically, this function is called with an .sfz filename. It can also be
   * used with a hydrogen \c drumkit.xml file. In this case the drumkit is loaded
   * by mapping hydrogen to sfz features. The format will be auto-detected,
   * it will be treated as hydrogen if it contains typical hydrogen tags.
   */
  bool load (const std::string& filename);

  /**
   * \brief List CCs supported by this .sfz file
   *
   * @returns a vector that contains information for each supported CC
   */
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
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
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
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
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
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void add_event_cc (uint time_frames, int channel, int cc, int value);

  /**
   * \brief Add a pitch bend event
   *
   * This function adds a pitch bend event to the event list that will be
   * rendered by the next process() call.
   *
   * Events must always be added sorted by event time (time_frames). The
   * pitch bend value must in range [0..16383], center value is 8192.
   *
   * @param time_frames the start time of the event (in frames)
   * @param channel     midi channel
   * @param value       new value for the controller [0..16383]
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void add_event_pitch_bend (uint time_frames, int channel, int value);

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
   *  - add_event_pitch_bend()
   *
   * These events will processed at the appropriate time positions (which means
   * we provide sample accurate timing for all events). Events must always be
   * added sorted by event time. After process() is done, the contents of event
   * buffer will be discarded.
   *
   * @param outputs    buffers for the output of the synthesizer
   * @param n_frames   number of stereo frames to be written
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void process (float **outputs, uint n_frames);

  /**
   * \brief All sound off
   *
   * Stop all active voices immediately. This may click because the voices are
   * not faded out.
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void all_sound_off();

  /**
   * \brief System reset
   *
   * Reset Synth state:
   *  - reset all CC values to their defaults
   *  - stop all active voices immediately
   *
   * This may click because the voices are not faded out.
   *
   * <em>This function is RT safe and can be used from the audio thread.</em>
   */
  void system_reset();

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

# Using multiple threads

The LiquidSFZ API, unless otherwise documented, doesn't use locks to make the
API thread safe. This means that you are responsible that <b>only one API
function is executed at a time</b>.

So you must not call a function (i.e. \ref Synth::process) in thread A and
another function (i.e. \ref Synth::load) in thread B so that both are running
at the same time. Instead you need to either use a mutex to ensure that only
one function is running at a time, or use other synchronization between the
threads A and B to ensure that only one thread uses the \ref Synth instance at
any point in time.

Note that if you have more than one \ref Synth instance (object), these can be
treated as isolated instances, i.e. different instances can be use concurrently
from different threads.

## The audio thread

Most applications will have one single real-time thread that generates the
audio output. In this real-time thread, only some of the API functions provided
by liquidsfz should be used, to avoid stalling the audio thread (for instance
by waiting for a lock, allocating memory or doing file I/O).

- \ref Synth::active_voice_count
- \ref Synth::add_event_note_on
- \ref Synth::add_event_note_off
- \ref Synth::add_event_cc
- \ref Synth::add_event_pitch_bend
- \ref Synth::all_sound_off
- \ref Synth::process
- \ref Synth::set_gain
- \ref Synth::system_reset

Typically these restrictions mean that at the beginning of your application,
you setup the \ref Synth object by loading an .sfz file, setting the sample
rate, number of voices and so forth. Eventually this is done, and you start a
real time thread that does audio I/O. Since you must ensure that only one
thread at a time is using the \ref Synth object, you would then only use
the functions of this list in the audio thread and no longer use any other
function from any other thread.

If you need to access one of the other functions (for instance for loading a
new .sfz file using \ref Synth::load), you would ensure that the audio thread
is no longer using the \ref Synth object. Then the setup phase would start
again, loading, setting sample rate and so forth, until you can restart the
audio thread.

## Callbacks from load

It is possible to request progress information for \ref Synth::load by using
\ref Synth::set_progress_function. If you enable this, the progress function
will be invoked in the same thread that you called \ref Synth::load.

## Callbacks from the logging framework

If you setup a custom log function using \ref Synth::set_log_function,
callbacks will occur in the threads that you call functions in. For example,
if you \ref Synth::load during initialization, you will get callbacks in the
thread you started load in.

Messages that are <b>relevant to end users</b> are errors, warnings and info
messages.  For these log levels, we guarantee that such log messages are never
produced from functions that can be run in the audio thread (real time thread).
These log messages are enabled by default.

This means that the callback set by \ref Synth::set_log_function doesn't need
to be realtime safe, as these callbacks only occur in non-rt-safe functions
such as \ref Synth::load.

Messages that are <b>relevant to developers</b> use the log level debug. These
can occur from any thread, including real time safe functions like \ref
Synth::process. Debug messages are only relevant for developers, so in almost
every case you should keep them disabled, which is the default. If you enable
debug messages using \ref Synth::set_log_level, you either need to provide an
realtime safe logging callback or accept that during development, debug
messages will break realtime capabilty.

*/

}
#endif
