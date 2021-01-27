/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2020-2021  Stefan Westerfeld
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

#include "argparser.hh"

namespace LiquidSFZInternal {

using std::vector;
using std::string;

bool
ArgParser::starts_with (const string& s, const string& start)
{
  return s.substr (0, start.size()) == start;
}

ArgParser::ArgParser (int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    m_args.push_back (argv[i]);
}

bool
ArgParser::parse_cmd (const string& cmd)
{
  for (auto it = m_args.begin(); it != m_args.end(); it++)
    {
      if (!it->empty() && (*it)[0] != '-')
        {
          if (*it == cmd)
            {
              m_args.erase (it);
              return true;
            }
          else /* first positional arg is not cmd */
            {
              return false;
            }
        }
    }
  return false;
}

bool
ArgParser::parse_opt (const string& option, string& out_s)
{
  bool found_option = false;
  auto it = m_args.begin();
  while (it != m_args.end())
    {
      auto next_it = it + 1;
      if (*it == option && next_it != m_args.end())   /* --option foo */
        {
          out_s = *next_it;
          next_it = m_args.erase (it, it + 2);
          found_option = true;
        }
      else if (starts_with (*it, (option + "=")))   /* --option=foo */
        {
          out_s = it->substr (option.size() + 1);
          next_it = m_args.erase (it);
          found_option = true;
        }
      it = next_it;
    }
  return found_option;
}

bool
ArgParser::parse_opt (const string& option, int& out_i)
{
  string out_s;
  if (parse_opt (option, out_s))
    {
      out_i = atoi (out_s.c_str());
      return true;
    }
  return false;
}

bool
ArgParser::parse_opt (const string& option, float& out_f)
{
  string out_s;
  if (parse_opt (option, out_s))
    {
      out_f = atof (out_s.c_str());
      return true;
    }
  return false;
}

bool
ArgParser::parse_opt (const string& option)
{
  for (auto it = m_args.begin(); it != m_args.end(); it++)
    {
      if (*it == option) /* --option */
        {
          m_args.erase (it);
          return true;
        }
    }
  return false;
}

bool
ArgParser::parse_args (size_t expected_count, vector<string>& out_args)
{
  if (m_args.size() == expected_count)
    {
      out_args = m_args;
      return true;
    }
  return false;
}

};
