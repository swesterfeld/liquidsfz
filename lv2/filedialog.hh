// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <sys/types.h>

class FileDialog
{
  int file_input_fd = -1;
  pid_t pid = -1;
  enum {
    NONE,
    ERROR,
    OPEN,
    DONE
  } state = NONE;

  constexpr static auto KDIALOG = "/usr/bin/kdialog";
  constexpr static auto YAD     = "/usr/bin/yad";
  constexpr static auto ZENITY  = "/usr/bin/zenity";

  static inline std::string     last_start_dir;
  static inline std::mutex last_start_dir_mutex;
  void
  set_last_start_dir (const std::string& filename)
  {
    std::lock_guard lg (last_start_dir_mutex);
    std::filesystem::path fs_path (std::filesystem::absolute (filename));
    last_start_dir = fs_path.parent_path().string();
  }
  std::string
  get_last_start_dir()
  {
    std::lock_guard lg (last_start_dir_mutex);
    std::string start_dir;
    if (last_start_dir != "" && std::filesystem::is_directory (last_start_dir))
      return last_start_dir;
    return ".";
  }
  bool is_kde_full_session();
  void restore_ardour_environment();
public:
  FileDialog (const std::string& title, const std::string& filter, const std::string& filter_exts, const std::string& zenity_filename);
  ~FileDialog();

  bool is_open ();
  static bool have_helpers();
  std::string get_filename();
};
