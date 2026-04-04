#include <string>
#include <functional>

using std::string;

class SfzReader
{
public:
  enum Warning
  {
    INCOMPLETE_OPCODE_ASSIGNMENT,
    EQUAL_SIGN_IN_OPCODE_VALUE,
    UNEXPECTED_CHARACTERS
  };
private:
string s;
int p = 0;

string
read_opcode()
{
  size_t start = p;

  const char *ss = s.c_str();
  while (isalnum (ss[p]) || ss[p] == '_')
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
  p++; // skip '<'

  size_t start = p;

  const char *ss = s.c_str();
  while (ss[p] != '>' && ss[p] != 0)
    p++;

  if (ss[p] == '>')
    {
      string tag (ss + start, p - start);
      p++; // skip '>'
      return tag;
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
              on_warning (EQUAL_SIGN_IN_OPCODE_VALUE);
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

void
skip_unexpected_characters()
{
  const char *ss = s.c_str();
  while (ss[p] && ss[p] != ' ' && ss[p] != '<')
    p++;
  return;
}

public:
void
parse (const string& line)
{
  s = line;
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

std::function<void(const std::string&)> on_tag;
std::function<void(const std::string&, const std::string&)> on_opcode;
std::function<void(Warning)> on_warning;

SfzReader()
{
  on_tag = [](const string&){};
  on_opcode = [](const string&, const string&){};
  on_warning = [](Warning){};
}

};
