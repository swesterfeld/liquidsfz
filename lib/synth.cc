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

namespace LiquidSFZInternal {

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
