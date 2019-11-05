/*
 * liquidsfz - sfz support using fluidsynth
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
  #define LIQUIDSFZ_COMP_CLANG
#elif __GNUC__ > 2
  #define LIQUIDSFZ_COMP_GCC
#else
  #error "unsupported compiler"
#endif

#ifdef LIQUIDSFZ_COMP_GCC
  #define LIQUIDSFZ_PRINTF(format_idx, arg_idx)      __attribute__ ((__format__ (gnu_printf, format_idx, arg_idx)))
#else
  #define LIQUIDSFZ_PRINTF(format_idx, arg_idx)      __attribute__ ((__format__ (__printf__, format_idx, arg_idx)))
#endif

#include <sys/time.h>
#include <math.h>

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

#endif /* LIQUIDSFZ_UTILS_HH */
