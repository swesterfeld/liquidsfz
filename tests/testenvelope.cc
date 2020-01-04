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

#include "envelope.hh"

using namespace LiquidSFZInternal;

int
main (int argc, char **argv)
{
  Envelope envelope;

  envelope.set_delay (atof (argv[1]));
  envelope.set_attack (atof (argv[2]));
  envelope.set_hold (atof (argv[3]));
  envelope.set_decay (atof (argv[4]));
  envelope.set_sustain (atof (argv[5]));
  envelope.set_release (atof (argv[6]));
  envelope.start (Region(), 48000);

  for (int i = 0; i < 24000; i++)
    printf ("%f\n", envelope.get_next());

  envelope.stop (OffMode::NORMAL);

  for (int i = 0; i < 24000; i++)
    printf ("%f\n", envelope.get_next());
}
