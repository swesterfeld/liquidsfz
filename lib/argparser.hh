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

#ifndef LIQUIDSFZ_ARGPARSER_HH
#define LIQUIDSFZ_ARGPARSER_HH

#include <vector>
#include <string>

namespace LiquidSFZInternal
{

class ArgParser
{
  std::vector<std::string> m_args;
  bool starts_with (const std::string& s, const std::string& start);
public:
  ArgParser (int argc, char **argv);

  bool parse_cmd (const std::string& cmd);
  bool parse_opt (const std::string& option, std::string& out_s);
  bool parse_opt (const std::string& option, int& out_i);
  bool parse_opt (const std::string& option, float& out_f);
  bool parse_opt (const std::string& option);
  bool parse_args (size_t expected_count, std::vector<std::string>& out_args);
};

}

#endif /* LIQUIDSFZ_ARGPARSER_HH */
