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
using std::string;

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
  if (argc < 3)
    {
      printf ("too few args\n");
      return 1;
    }

  string cmd = argv[1];
  if (argc == 5 && cmd == "sweep")
    {
      vector<float> left;
      vector<float> right;
      vector<float> freq;
      gen_sweep (left, right, freq);

      Filter filter;
      filter.set_type (Filter::type_from_string (argv[2]));
      filter.set_sample_rate (48000);
      filter.update_config (atof (argv[3]), atof (argv[4]));
      filter.reset();
      filter.process (&left[0], &right[0], left.size());

      for (size_t i = 0; i < left.size(); i++)
        printf ("%f %.17g\n", freq[i], sqrt (left[i] * left[i] + right[i] * right[i]));

      return 0;
    }
  else if (argc == 5 && cmd == "ir")
    {
      vector<float> left = { 1 };
      vector<float> right;

      left.resize (48000);
      right.resize (48000);

      Filter filter;
      filter.set_type (Filter::type_from_string (argv[2]));
      filter.set_sample_rate (48000);
      filter.update_config (atof (argv[3]), atof (argv[4]));
      filter.reset();
      filter.process (&left[0], &right[0], left.size());

      for (size_t i = 0; i < left.size(); i++)
        printf ("%.17g\n", left[i]);

      return 0;
    }
  else if (argc == 5 && cmd == "sines")
    {
      Filter filter;
      filter.set_type (Filter::type_from_string (argv[2]));
      filter.set_sample_rate (48000);
      filter.update_config (atof (argv[3]), atof (argv[4]));

      double phase = 0;
      for (double f = 20; f < 24000; f *= 1.1)
        {
          vector<float> left;
          vector<float> right;
          for (int i = 0; i < 48000; i++)
            {
              left.push_back (sin (phase));
              right.push_back (cos (phase));
              phase += f / 48000 * 2 * M_PI;
            }
          filter.reset();
          filter.process (&left[0], &right[0], left.size());

          printf ("%f %.17g\n", f, sqrt (left.back() * left.back() + right.back() * right.back()));
        }
    }
}
