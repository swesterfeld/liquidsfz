// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <string>
#include <vector>
#include <sys/types.h>

// Owns the helper PID and its output pipe. Polling never waits for output or
// child exit; destroying an unfinished dialog kills and reaps the helper.
class FileDialog
{
  int file_input_fd = -1;
  pid_t pid = -1;
  bool open = false;
  bool failed = false;
  std::string output;
  const std::vector<std::string> environment;

  bool is_kde_full_session();
  void spawn (std::vector<std::string> args);
public:
  FileDialog (const std::string& title, const std::string& filter,
              const std::string& filter_exts, const std::string& zenity_filename);
  // Run a helper with prepared arguments (also used by the subprocess tests).
  explicit FileDialog (const std::vector<std::string>& args);
  ~FileDialog();

  FileDialog (const FileDialog&) = delete;
  FileDialog& operator= (const FileDialog&) = delete;

  bool is_open() const { return open; }
  static bool have_helpers();
  std::string get_filename();
};
