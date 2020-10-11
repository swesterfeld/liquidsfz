/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019-2020  Stefan Westerfeld
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

#include "hydrogenimport.hh"

using namespace LiquidSFZInternal;

using std::string;

int
main (int argc, char **argv)
{
  Synth synth; // dummy for testing
  HydrogenImport himport (&synth);
  string out;
  if (!himport.detect (argv[1]))
    {
      printf ("This is not a hydrogen file\n");
      return 1;
    }
  himport.parse (argv[1], out);
  printf ("/////////////////////////////////////////////////////////////////////////////////////\n");
  printf ("%s", out.c_str());
  printf ("/////////////////////////////////////////////////////////////////////////////////////\n");
}
