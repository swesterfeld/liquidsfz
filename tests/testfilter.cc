/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2020  Stefan Westerfeld
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

#include "filter.hh"

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstring>

#include <vector>

using namespace LiquidSFZInternal;

using std::vector;

void
gen_sweep (vector<float>& left, vector<float>& right, vector<float>& freq)
{
  double phase = 0;
  double l = 48000 * 5;
  double factor = pow (24000 / 20., (1./l));
  double vol = 0;
  for (double f = 20; f < 24000; f *= factor)
    {
      freq.push_back (f);
      left.push_back (sin (phase) * vol);
      right.push_back (cos (phase) * vol);
      phase += f / 48000 * 2 * M_PI;
      vol += 1. / 500; /* avoid click at start */
      if (vol > 1)
        vol = 1;
    }
}

int
main (int argc, char **argv)
{
  if (argc == 3)
    {
      Filter filter;
      filter.set_type (Filter::type_from_string (argv[1]));

      vector<float> left;
      vector<float> right;
      vector<float> freq;
      gen_sweep (left, right, freq);

      filter.set_sample_rate (48000);
      filter.reset (atof (argv[2]));
      filter.process (&left[0], &right[0], left.size());

      for (size_t i = 0; i < left.size(); i++)
        printf ("%f %f\n", freq[i], sqrt (left[i] * left[i] + right[i] * right[i]));

      return 0;
    }
}
