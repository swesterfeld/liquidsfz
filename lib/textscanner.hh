// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <string_view>

namespace LiquidSFZInternal
{

// The input must outlive the scanner and its captures. A failed operation
// leaves the cursor and capture unchanged; subsequent operations do nothing.
class TextScanner
{
  std::string_view input_;
  size_t position_ = 0;
  bool ok_ = true;

  static bool
  is_space (char c)
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  }

  template<typename Predicate>
  void
  scan (std::string_view& result, Predicate predicate)
  {
    if (!ok_)
      return;
    size_t end = position_;
    while (end < input_.size() && predicate (input_[end]))
      end++;
    if (end == position_)
      {
        ok_ = false;
        return;
      }
    result = input_.substr (position_, end - position_);
    position_ = end;
  }

public:
  explicit TextScanner (std::string_view input) : input_ (input) {}

  void
  literal (std::string_view text)
  {
    if (!ok_)
      return;
    if (input_.substr (position_, text.size()) != text)
      ok_ = false;
    else
      position_ += text.size();
  }

  void
  digits (std::string_view& result)
  {
    scan (result, [] (char c) { return c >= '0' && c <= '9'; });
  }

  void
  token (std::string_view& result)
  {
    scan (result, [] (char c) { return !is_space (c); });
  }

  void
  spaces()
  {
    std::string_view ignored;
    scan (ignored, is_space);
  }

  void
  skip_spaces()
  {
    if (!ok_)
      return;
    while (position_ < input_.size() && is_space (input_[position_]))
      position_++;
  }

  // Quotes have no escape syntax, matching SFZ preprocessor includes.
  void
  quoted (std::string_view& result)
  {
    if (!ok_)
      return;
    if (position_ == input_.size() || input_[position_] != '"')
      {
        ok_ = false;
        return;
      }
    const size_t end = input_.find ('"', position_ + 1);
    if (end == std::string_view::npos)
      {
        ok_ = false;
        return;
      }
    result = input_.substr (position_ + 1, end - position_ - 1);
    position_ = end + 1;
  }

  void end() { if (position_ != input_.size()) ok_ = false; }
  bool ok() const { return ok_; }
  size_t position() const { return position_; }
};

}
