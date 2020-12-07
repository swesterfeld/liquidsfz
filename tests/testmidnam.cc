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

#include "synth.hh"
#include "liquidsfz.hh"
#include "midnam.hh"

using namespace LiquidSFZ;

using LiquidSFZInternal::gen_midnam;

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: testliquid <sfz_filename>\n");
      return 1;
    }
  Synth synth;
  synth.set_sample_rate (48000);
  if (!synth.load (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  printf ("%s\n", gen_midnam (synth, "model").c_str());
}
