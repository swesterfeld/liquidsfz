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

#include "synth.hh"
#include "utils.hh"

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: testperf <sfz_filename>\n");
      return 1;
    }
  Synth synth;
  synth.set_sample_rate (48000);
  if (!synth.sfz_loader.parse (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }

  std::vector<float> out_left (1024), out_right (1024);

  float *outputs[2] = { out_left.data(), out_right.data() };
  unsigned char note_on[3] = { 0x90, 60, 127 };
  synth.add_midi_event (0, note_on);
  const double time_start = get_time();
  uint samples = 0;
  for (int pos = 0; pos < 1000; pos++)
    {
      synth.process (outputs, 1024);
      samples += 1024;
    }
  const double time_total = get_time() - time_start;
  printf ("time %f, samples %d, ns/sample %f bogo_voices %f\n", time_total, samples, time_total * 1e9 / samples, (samples / 48000.) / time_total);
}
