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

#ifndef LIQUIDSFZ_UTILS_HH
#define LIQUIDSFZ_UTILS_HH

// detect compiler
#if __clang__
  #define LIQUIDSFZ_COMP_CLANG 1
#elif __GNUC__ > 2
  #define LIQUIDSFZ_COMP_GCC 1
#else
  #error "unsupported compiler"
#endif

#if LIQUIDSFZ_COMP_GCC
  #define LIQUIDSFZ_PRINTF(format_idx, arg_idx)      __attribute__ ((__format__ (gnu_printf, format_idx, arg_idx)))
#else
  #define LIQUIDSFZ_PRINTF(format_idx, arg_idx)      __attribute__ ((__format__ (__printf__, format_idx, arg_idx)))
#endif

#if WIN32
  #define LIQUIDSFZ_OS_WINDOWS 1
#endif

#include <sys/time.h>
#include <math.h>

#include <string>


typedef unsigned int uint;

namespace LiquidSFZInternal
{

inline double
db_to_factor (double dB)
{
  return pow (10, dB / 20);
}

inline double
db_from_factor (double factor, double min_dB)
{
  if (factor > 0)
    return 20 * log10 (factor);
  else
    return min_dB;
}

inline double
get_time()
{
  /* return timestamp in seconds as double */
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

double string_to_double (const std::string& str);

static constexpr char PATH_SEPARATOR = '/';

bool path_is_absolute (const std::string& filename);
std::string path_absolute (const std::string& filename);
std::string path_dirname (const std::string& filename);
std::string path_join (const std::string& path1, const std::string& path2);

class LinearSmooth
{
  float value_ = 0;
  float linear_value_ = 0;
  float linear_step_ = 0;
  uint  total_steps_ = 1;
  uint  steps_ = 0;
public:
  void
  reset (uint rate, float time)
  {
    total_steps_ = std::max<int> (rate * time, 1);
  }
  void
  set (float new_value, bool now = false)
  {
    if (now)
      {
        steps_ = 0;
        value_ = new_value;
      }
    else if (new_value != value_)
      {
        if (!steps_)
          linear_value_ = value_;

        linear_step_ = (new_value - linear_value_) / total_steps_;
        steps_ = total_steps_;
        value_ = new_value;
      }
  }
  float
  get_next()
  {
    if (!steps_)
      return value_;
    else
      {
        steps_--;
        linear_value_ += linear_step_;
        return linear_value_;
      }
  }
  bool
  is_constant()
  {
    return steps_ == 0;
  }
};

}

#endif /* LIQUIDSFZ_UTILS_HH */
