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

string
SFZReader::read_opcode()
{
  size_t start = p;

  while (isalnum (s[p]) || s[p] == '_')
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
SFZReader::read_opcode_value()
{
  size_t start = p;
  int last_space = -1;

  for (;;)
    {
      if (s[p] == 0)
        {
          return string (s + start, p - start);
        }
      else if (s[p] == ' ')
        {
          last_space = p;
          p++;
        }
      else if (s[p] == '=')
        {
          if (last_space != -1)
            {
              p = last_space;
              return string (s + start, last_space - start);
            }
          else
            {
              on_warning (EQUAL_SIGN_IN_OPCODE_VALUE);
              p++;
            }
        }
      else if (s[p] == '<')
        {
          return string (s + start, p - start);
        }
      else
        {
          p++;
        }
    }
  return "";
}

void
SFZReader::skip_unexpected_characters()
{
  while (s[p] && s[p] != ' ' && s[p] != '<')
    p++;
  return;
}

void
SFZReader::parse (const string& line)
{
  s = line.c_str();
  p = 0;
  for (;;)
    {
      if (isalnum (s[p]))
        {
          string opcode = read_opcode();
          if (!opcode.empty())
            {
              string opcode_value = read_opcode_value();
              if (!opcode_value.empty())
                on_opcode (opcode, opcode_value);
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
      else if (s[p] == ' ')
        p++;
      else if (s[p] == 0)
        break;
      else
        {
          on_warning (UNEXPECTED_CHARACTERS);
          skip_unexpected_characters();
        }
    }
}

}
