// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

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
