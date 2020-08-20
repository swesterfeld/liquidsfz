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

#include "curve.hh"

#include <cstdlib>
#include <cstdio>
#include <cassert>

using namespace LiquidSFZInternal;

int
main (int argc, char **argv)
{
  if (argc > 2)
    {
      Curve curve;
      for (int a = 1; a < argc; a += 2)
        {
          printf ("%d %f #p\n", atoi (argv[a]), atof (argv[a + 1]));
          curve.points.push_back ({ atoi (argv[a]), atof (argv[a + 1]) });
        }
      CurveTable curve_table;
      curve_table.expand_curve (curve);
      for (int i = 0; i < 128; i++)
        printf ("%d %f #i\n", i, curve.table->at (i));
      return 0;
    }
  Curve curve;
  curve.set (32, 0.3);
  curve.set (64, 0.4);
  curve.set (65, 0.5);
  curve.set (66, 0.6);

  Curve curve2 (curve);
  CurveTable curve_table;

  Curve curve3;
  curve3.set (96, 1);

  /* test deduplication */
  curve_table.expand_curve (curve);
  curve_table.expand_curve (curve2);
  curve_table.expand_curve (curve3);
  assert (curve.table == curve2.table);
  assert (curve.table != curve3.table);

  for (auto p : curve.points)
    printf ("%d %f #p\n", p.first, p.second);
  for (int i = 0; i < 128; i++)
    printf ("%d %f #i\n", i, curve.get (i));

}
