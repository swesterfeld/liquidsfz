#include "log.hh"
#include <stdio.h>
#include <stdarg.h>

#include <string>

using namespace LiquidSFZ;

enum class Log
{
  DEBUG,
  INFO,
  WARNING,
  ERROR
};

using std::string;

static string
log2str (Log level)
{
  switch (level)
    {
      case Log::DEBUG:    return "debug";
      case Log::INFO:     return "info";
      case Log::WARNING:  return "warning";
      case Log::ERROR:    return "error";
    }
}

static string
string_vprintf (const char *format, va_list vargs)
{
  string s;

  char *str = NULL;
  if (vasprintf (&str, format, vargs) >= 0 && str)
    {
      s = str;
      free (str);
    }
  else
    s = format;

  return s;
}

static inline void
logv (Log level, const char *format, va_list vargs)
{
  string s = string_vprintf (format, vargs);
  fprintf (stderr, "[%s] %s", log2str (level).c_str(), s.c_str());
}

void
LiquidSFZ::log_error (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  logv (Log::ERROR, format, ap);
  va_end (ap);
}

void
LiquidSFZ::log_warning (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  logv (Log::WARNING, format, ap);
  va_end (ap);
}

void
LiquidSFZ::log_info (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  logv (Log::INFO, format, ap);
  va_end (ap);
}

void
LiquidSFZ::log_debug (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  logv (Log::DEBUG, format, ap);
  va_end (ap);
}

string
LiquidSFZ::string_printf (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  string s = string_vprintf (format, ap);
  va_end (ap);

  return s;
}
