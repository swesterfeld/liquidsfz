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

#include "synth.hh"
#include "liquidsfz.hh"

#include <stdarg.h>

using LiquidSFZ::Log;

using std::string;
using std::min;

namespace LiquidSFZInternal {

void
Synth::process_audio (float **outputs, uint n_frames, uint offset)
{
  float *outputs_offset[] = {
    outputs[0] + offset,
    outputs[1] + offset
  };

  for (auto& voice : voices_)
    {
      if (voice.state_ != Voice::IDLE)
        voice.process (outputs_offset, n_frames);
    }
  global_frame_count += n_frames;
}

void
Synth::process (float **outputs, uint n_frames)
{
  std::fill_n (outputs[0], n_frames, 0.0);
  std::fill_n (outputs[1], n_frames, 0.0);

  uint offset = 0;
  for (const auto& event : events)
    {
      // ensure that new offset from event is not larger than n_frames
      const uint new_offset = min <uint> (event.offset, n_frames);

      // process any audio that is before the event
      process_audio (outputs, new_offset - offset, offset);
      offset = new_offset;

      // process event at timestamp offset
      switch (event.type)
        {
          case Event::Type::NOTE_ON:  note_on (event.channel, event.arg1, event.arg2);
                                      break;
          case Event::Type::NOTE_OFF: note_off (event.channel, event.arg1);
                                      break;
          case Event::Type::CC:       update_cc (event.channel, event.arg1, event.arg2);
                                      break;
          default:                    error ("unsupported event type %d\n", int (event.type));
        }
    }
  events.clear();

  // process frames after last event
  process_audio (outputs, n_frames - offset, offset);
}

void
Synth::set_log_function (std::function<void(Log, const char *)> function)
{
  log_function_ = function;
}

void
Synth::set_log_level (Log log_level)
{
  log_level_ = log_level;
}

static const char *
log2str (Log level)
{
  switch (level)
    {
      case Log::DEBUG:    return "liquidsfz::debug";
      case Log::INFO:     return "liquidsfz::info";
      case Log::WARNING:  return "liquidsfz::warning";
      case Log::ERROR:    return "liquidsfz::error";
      default:            return "***loglevel?***";
    }
}

void
Synth::logv (Log level, const char *format, va_list vargs)
{
  char buffer[1024];

  vsnprintf (buffer, sizeof (buffer), format, vargs);

  if (log_function_)
    log_function_ (level, buffer);
  else
    fprintf (stderr, "[%s] %s", log2str (level), buffer);
}

void
Synth::error (const char *format, ...)
{
  if (log_level_ <= Log::ERROR)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::ERROR, format, ap);
      va_end (ap);
    }
}

void
Synth::warning (const char *format, ...)
{
  if (log_level_ <= Log::WARNING)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::WARNING, format, ap);
      va_end (ap);
    }
}

void
Synth::info (const char *format, ...)
{
  if (log_level_ <= Log::INFO)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::INFO, format, ap);
      va_end (ap);
    }
}

void
Synth::debug (const char *format, ...)
{
  if (log_level_ <= Log::DEBUG)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::DEBUG, format, ap);
      va_end (ap);
    }
}

}
