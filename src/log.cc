#include "log.hh"
#include <stdio.h>
#include <stdarg.h>

#include <string>

using namespace LiquidSFZ;

static inline void
logv (Log level, const char *format, va_list vargs)
{
  std::string s;

  char *str = NULL;
  if (vasprintf (&str, format, vargs) >= 0 && str)
    {
      s = str;
      free (str);
    }
  else
    s = format;

  fprintf (stderr, "%s", s.c_str());
}

void
LiquidSFZ::log_printf (Log level, const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  logv (level, fmt, ap);
  va_end (ap);
}


