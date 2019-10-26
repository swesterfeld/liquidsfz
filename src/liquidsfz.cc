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

#include <fluidsynth.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <vector>
#include <string>
#include <regex>
#include <random>
#include "loader.hh"
#include "utils.hh"
#include "synth.hh"

using std::vector;
using std::string;
using std::regex;
using std::regex_replace;

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: liquidsfz <sfz_filename>\n");
      return 1;
    }
  jack_client_t *client = jack_client_open ("liquidsfz", JackNullOption, NULL);
  if (!client)
    {
       fprintf (stderr, "liquidsfz: unable to connect to jack server\n");
       exit (1);
     }
  Synth sfz_synth;
  if (!sfz_synth.sfz_loader.parse (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  sfz_synth.midi_input_port = jack_port_register (client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
  sfz_synth.audio_left = jack_port_register (client, "audio_out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  sfz_synth.audio_right = jack_port_register (client, "audio_out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  jack_set_process_callback (client,
    [](jack_nframes_t nframes, void *arg)
      {
        auto synth = static_cast<Synth *> (arg);
        return synth->process (nframes);
      }, &sfz_synth);
  if (jack_activate (client))
    {
      fprintf (stderr, "cannot activate client");
      exit (1);
    }

  fluid_settings_t *settings = new_fluid_settings();
  fluid_settings_setnum (settings, "synth.sample-rate", jack_get_sample_rate (client));
  fluid_settings_setnum (settings, "synth.gain", db_to_factor (Synth::VOLUME_HEADROOM_DB));
  fluid_settings_setint (settings, "synth.reverb.active", 0);
  fluid_settings_setint (settings, "synth.chorus.active", 0);
  sfz_synth.synth = new_fluid_synth (settings);

  printf ("Synthesizer running - press \"Enter\" to quit.\n");
  getchar();

  jack_client_close (client);

  delete_fluid_synth (sfz_synth.synth);
  delete_fluid_settings (settings);

  return 0;
}
