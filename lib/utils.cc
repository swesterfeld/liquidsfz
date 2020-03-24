/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019  Stefan Westerfeld
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

#include "utils.hh"
#include <unistd.h>
#include <libgen.h>

#include <vector>
#include <sstream>

using std::string;
using std::vector;

namespace LiquidSFZInternal
{

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
  if (filename.size() && filename[0] == PATH_SEPARATOR) // already absolute?
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
