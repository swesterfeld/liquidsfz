#include <string>
#include <functional>

using std::string;

class SfzReader
{

string s;
int p = 0;

string
read_opcode()
{
  size_t start = p;

  const char *ss = s.c_str();
  while (isalnum (ss[p]))
    p++;

  if (ss[p] == '=')
    {
      string opcode (ss + start, p - start);
      p++; // skip '='
      return opcode;
    }

  return "";
}

string
read_tag()
{
  size_t start = p;

  const char *ss = s.c_str();
  while (ss[p] != '>' && ss[p] != 0)
    p++;

  if (ss[p] == '>')
    {
      p++;
      return string (ss + start, p - start);
    }

  return "";
}

string
read_opcode_value()
{
  int value_start = p;
  int last_space = -1;

  const char *ss = s.c_str();

  for (;;)
    {
      if (ss[p] == 0)
        {
          return s.substr (value_start);
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
              return string (ss + value_start, last_space - value_start);
            }
          else
            {
              p++;
            }
        }
      else if (s[p] == '<')
        {
          return string (ss + value_start, p - value_start);
        }
      else
        {
          p++;
        }
    }
  return "";
}

public:
void
parse (const string& line)
{
  s = line;
  for (;;)
    {
      if (isalnum (s[p]))
        {
          string opcode = read_opcode();
          if (!opcode.empty())
            {
              string opcode_value = read_opcode_value();
              if (!opcode_value.empty() && on_opcode)
                on_opcode (opcode, opcode_value);
            }
        }
      else if (s[p] == '<')
        {
          string tag = read_tag();
          if (!tag.empty() && on_tag)
            on_tag (tag);
        }
      else if (s[p] == ' ')
        p++;
      else if (s[p] == 0)
        break;
    }
}

std::function<void(const std::string&)> on_tag;
std::function<void(const std::string&, const std::string&)> on_opcode;
};
