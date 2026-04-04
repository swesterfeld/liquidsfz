// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <string>
#include <functional>

namespace LiquidSFZInternal {

class SFZReader
{
public:
  enum Warning
  {
    INCOMPLETE_OPCODE_ASSIGNMENT,
    EQUAL_SIGN_IN_OPCODE_VALUE,
    MISSING_OPCODE_VALUE,
    UNEXPECTED_CHARACTERS
  };
private:
  const char *s = nullptr;
  int         p = 0;

  std::string strip_spaces (const char *str, size_t len);

  std::string read_opcode();
  std::string read_tag();
  std::string read_opcode_value();

  void skip_unexpected_characters();
public:
  SFZReader();

  std::function<void(const std::string&)> on_tag;
  std::function<void(const std::string&, const std::string&)> on_opcode;
  std::function<void(Warning)> on_warning;

  void parse (const std::string& line);
};

}
