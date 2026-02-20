// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "utils.hh"
#include <unistd.h>
#include <libgen.h>

#include <vector>
#include <sstream>
#include <filesystem>
#include <string>
#include <algorithm>
#include <optional>

using std::string;
using std::vector;

namespace LiquidSFZInternal
{

bool
path_is_absolute (const string& filename)
{
  if (filename.size() && filename[0] == PATH_SEPARATOR)
    return true;
#if LIQUIDSFZ_OS_WINDOWS
  if (filename.size() >= 2 && isalpha (filename[0]) && filename[1] == ':')
    return true;
#endif
  return false;
}

/*
 * NOTE: path_absolute and path_dirname are currently not portable to windows
 *
 * ideally we would use std::filesystem, but below gcc-9 this requires an
 * extra static library (-lstdc++fs) which causes problems with static linking
 *
 * std::filesystem is also not-so-well-supported with gcc-7
 */
string
path_absolute (const string& filename)
{
  if (path_is_absolute (filename))
    return filename;
  char buffer[2048];
  if (getcwd (buffer, sizeof (buffer)))
    return std::string (buffer) + PATH_SEPARATOR + filename;
  return filename; // failed to build absolute path
}

string
path_dirname (const string& filename)
{
  // make a copy first - dirname modifies its input string
  vector<char> buffer (filename.size() + 1);
  std::copy (filename.begin(), filename.end(), buffer.begin());

  return dirname (buffer.data());
}

string
path_join (const string& path1, const string& path2)
{
  return path1 + PATH_SEPARATOR + path2;
}

namespace fs = std::filesystem;

static string
to_lower (const string& s)
{
  string r = s;
  transform (r.begin(), r.end(), r.begin(), [](unsigned char c) { return tolower(c); });
  return r;
}

// Case-insensitive filename lookup inside one directory
static std::optional<fs::path>
find_case_insensitive (const fs::path& directory, const string& target_name)
{
  string target_lower = to_lower (target_name);

  for (const auto& entry : fs::directory_iterator (directory))
    {
      const string name = entry.path().filename().string();

      if (to_lower (name) == target_lower)
        return entry.path().filename();
    }
  return std::nullopt;
}

string
path_resolve_case_insensitive (const string& path)
{
  fs::path input_path (path);

  // resolve quickly if case matches exactly
  if (fs::exists (input_path))
      return fs::canonical (input_path);

  fs::path current;

  if (input_path.is_absolute())
    current = input_path.root_path();
  else
    current = fs::current_path();

  for (const auto& part : input_path.relative_path())
    {
      // if exact subpath exists, take it directly
      fs::path candidate = current / part;
      if (fs::exists (candidate))
        {
          current = candidate;
          continue;
        }

      // otherwise fall back to case-insensitive search
      if (!fs::exists (current) || !fs::is_directory (current))
        return path; // fail -> caller will see open failed for path

      auto match = find_case_insensitive (current, part.string());
      if (!match)
        return path; // fail -> caller will see open failed for path

      current /= *match;
    }
  return current;
}

/*
 * locale independent string to double conversion
 *
 * ideally, this would use std::from_chars, but g++-7 and g++-8 don't implement
 * the double case, so this is relatively slow, but good enough
 */

double
string_to_double (const string& str)
{
  double d = 0;
  std::istringstream is (str);
  is.imbue (std::locale::classic());
  is >> d;
  return d;
}

}
