#include "log.hh"
#include <stdio.h>
#include <stdarg.h>

#include <string>
#include <vector>

namespace LiquidSFZInternal
{

using std::string;
using std::vector;

string
string_printf (const char *format, ...)
{
  vector<char> buffer;
  va_list ap;

  /* figure out required size */
  va_start (ap, format);
  int size = vsnprintf (nullptr, 0, format, ap);
  va_end (ap);

  if (size < 0)
    return format;

  /* now print with large enough buffer */
  buffer.resize (size + 1);

  va_start (ap, format);
  size = vsnprintf (&buffer[0], buffer.size(), format, ap);
  va_end (ap);

  if (size < 0)
    return format;

  return &buffer[0];
}

}
