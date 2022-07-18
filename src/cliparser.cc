// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <stdlib.h>
#include <assert.h>
#include <string>
#include <vector>

#include "cliparser.hh"

using std::string;
using std::vector;

static bool
string_chars (char ch)
{
  if ((ch >= 'A' && ch <= 'Z')
  ||  (ch >= '0' && ch <= '9')
  ||  (ch >= 'a' && ch <= 'z')
  ||  (ch == '.')
  ||  (ch == ':')
  ||  (ch == '=')
  ||  (ch == '/')
  ||  (ch == '-')
  ||  (ch == '_'))
    return true;

  return false;
}

static bool
white_space (char ch)
{
  return (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r');
}

bool
CLIParser::parse (const string& line)
{
  bool ok = tokenize (line);
  tokenizer_error = !ok;
  return ok;
}

bool
CLIParser::tokenize (const string& line)
{
  enum { BLANK, STRING, QUOTED_STRING, QUOTED_STRING_ESCAPED, COMMENT } state = BLANK;
  string s;

  string xline = line + '\n';
  tokens.clear();
  for (string::const_iterator i = xline.begin(); i != xline.end(); i++)
    {
      if (state == BLANK && string_chars (*i))
        {
          state = STRING;
          s += *i;
        }
      else if (state == BLANK && *i == '"')
        {
          state = QUOTED_STRING;
        }
      else if (state == BLANK && white_space (*i))
        {
          // ignore more whitespaces if we've already seen one
        }
      else if (state == STRING && string_chars (*i))
        {
          s += *i;
        }
      else if ((state == STRING && white_space (*i))
           ||  (state == QUOTED_STRING && *i == '"'))
        {
          tokens.push_back (s);
          s = "";
          state = BLANK;
        }
      else if (state == QUOTED_STRING && *i == '\\')
        {
          state = QUOTED_STRING_ESCAPED;
        }
      else if (state == QUOTED_STRING)
        {
          s += *i;
        }
      else if (state == QUOTED_STRING_ESCAPED)
        {
          s += *i;
          state = QUOTED_STRING;
        }
      else if (*i == '#')
        {
          state = COMMENT;
        }
      else if (state == COMMENT)
        {
          // ignore comments
        }
      else
        {
          return false;
        }
    }
  return state == BLANK || state == COMMENT;
}

bool
CLIParser::convert (const std::string& token, int& arg)
{
  arg = atoi (token.c_str());
  return true;
}

bool
CLIParser::convert (const std::string& token, double& arg)
{
  arg = atof (token.c_str());
  return true;
}

bool
CLIParser::convert (const std::string& token, std::string& arg)
{
  arg = token;
  return true;
}
