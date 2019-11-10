#include "log.hh"
#include <stdio.h>
#include <stdarg.h>

#include <string>

namespace LiquidSFZInternal
{

using std::string;

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

string
string_printf (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);
  string s = string_vprintf (format, ap);
  va_end (ap);

  return s;
}

}
