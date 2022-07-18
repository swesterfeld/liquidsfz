// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#ifndef LIQUIDSFZ_CLI_PARSER_HH
#define LIQUIDSFZ_CLI_PARSER_HH

class CLIParser
{
private:
  std::vector<std::string> tokens;
  bool                     tokenizer_error;

  bool convert (const std::string& token, int& arg);
  bool convert (const std::string& token, double& arg);
  bool convert (const std::string& token, std::string& arg);
  bool tokenize (const std::string& line);

public:
  bool parse (const std::string& line);

  bool empty_line()
  {
    return !tokenizer_error && !tokens.size();
  }
  bool command (const std::string& cmd)
  {
    if (tokenizer_error || tokens.size() != 1 || cmd != tokens[0])
      return false;
    return true;
  }
  template<class T1>
  bool command (const std::string& cmd, T1& arg1)
  {
    if (tokenizer_error || tokens.size() != 2 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1);
  }
  template<class T1, class T2>
  bool command (const std::string& cmd, T1& arg1, T2& arg2)
  {
    if (tokenizer_error || tokens.size() != 3 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1) && convert (tokens[2], arg2);
  }
  template<class T1, class T2, class T3>
  bool command (const std::string& cmd, T1& arg1, T2& arg2, T3& arg3)
  {
    if (tokenizer_error || tokens.size() != 4 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1) && convert (tokens[2], arg2) && convert (tokens[3], arg3);
  }
  template<class T1, class T2, class T3, class T4>
  bool command (const std::string& cmd, T1& arg1, T2& arg2, T3& arg3, T4& arg4)
  {
    if (tokenizer_error || tokens.size() != 5 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1) && convert (tokens[2], arg2) && convert (tokens[3], arg3)
      &&   convert (tokens[4], arg4);
  }
  template<class T1, class T2, class T3, class T4, class T5>
  bool command (const std::string& cmd, T1& arg1, T2& arg2, T3& arg3, T4& arg4, T5& arg5)
  {
    if (tokenizer_error || tokens.size() != 6 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1) && convert (tokens[2], arg2) && convert (tokens[3], arg3)
      &&   convert (tokens[4], arg4) && convert (tokens[5], arg5);
  }
  template<class T1, class T2, class T3, class T4, class T5, class T6>
  bool command (const std::string& cmd, T1& arg1, T2& arg2, T3& arg3, T4& arg4, T5& arg5, T6& arg6)
  {
    if (tokenizer_error || tokens.size() != 7 || cmd != tokens[0])
      return false;
    return convert (tokens[1], arg1) && convert (tokens[2], arg2) && convert (tokens[3], arg3)
      &&   convert (tokens[4], arg4) && convert (tokens[5], arg5) && convert (tokens[6], arg6);
  }
};

#endif /* LIQUIDSFZ_CLI_PARSER_HH */
