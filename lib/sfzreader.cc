// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "sfzreader.hh"

using std::string;

namespace LiquidSFZInternal {

SFZReader::SFZReader()
{
  on_tag = [](const string&){};
  on_opcode = [](const string&, const string&){};
  on_warning = [](Warning){};
}

bool
SFZReader::x_isalnum (char c) /* not locale dependent */
{
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

bool
SFZReader::x_isspace (char c) /* not locale dependent */
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

string
SFZReader::read_opcode()
{
  size_t start = p;

  while (x_isalnum (s[p]) || s[p] == '_')
    p++;

  if (s[p] == '=')
    {
      string opcode (s + start, p - start);
      p++; // skip '='
      return opcode;
    }

  return "";
}

string
SFZReader::read_tag()
{
  p++; // skip '<'

  size_t start = p;

  while (s[p] != '>' && s[p] != 0)
    p++;

  if (s[p] == '>')
    {
      string tag (s + start, p - start);
      p++; // skip '>'
      return tag;
    }

  return "";
}

string
SFZReader::strip_spaces (const char *str, size_t len)
{
  while (len && x_isspace (str[0])) // strip leading whitespace
    {
      str++;
      len--;
    }
  while (len && x_isspace (str[len - 1])) // strip trailing whitespace
    {
      len--;
    }
  return string (str, len);
}

string
SFZReader::read_opcode_value()
{
  size_t start = p;
  int last_space = -1;

  while (s[p])
    {
      if (x_isspace (s[p]))
        {
          last_space = p;
          p++;
        }
      else if (s[p] == '=')
        {
          if (last_space != -1)
            {
              p = last_space;
              return strip_spaces (s + start, last_space - start);
            }
          else
            {
              on_warning (EQUAL_SIGN_IN_OPCODE_VALUE);
              p++;
            }
        }
      else if (s[p] == '<')
        {
          return strip_spaces (s + start, p - start);
        }
      else
        {
          p++;
        }
    }
  return strip_spaces (s + start, p - start);
}

void
SFZReader::skip_unexpected_characters()
{
  while (s[p] && !x_isspace (s[p]) && s[p] != '<')
    p++;
  return;
}

void
SFZReader::parse (const string& line)
{
  s = line.c_str();
  p = 0;
  while (s[p])
    {
      if (x_isalnum (s[p]))
        {
          string opcode = read_opcode();
          if (!opcode.empty())
            {
              string opcode_value = read_opcode_value();
              if (!opcode_value.empty())
                {
                  on_opcode (opcode, opcode_value);
                }
              else
                {
                  on_warning (MISSING_OPCODE_VALUE);
                }
            }
          else
            {
              on_warning (INCOMPLETE_OPCODE_ASSIGNMENT);
            }
        }
      else if (s[p] == '<')
        {
          string tag = read_tag();
          if (!tag.empty())
            on_tag (tag);
        }
      else if (x_isspace (s[p]))
        {
          p++;
        }
      else
        {
          on_warning (UNEXPECTED_CHARACTERS);
          skip_unexpected_characters();
        }
    }
}

}
