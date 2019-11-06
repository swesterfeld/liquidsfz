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

using namespace LiquidSFZ;

class JackStandalone
{
  jack_client_t *client = nullptr;
  jack_port_t *midi_input_port = nullptr;
  jack_port_t *audio_left = nullptr;
  jack_port_t *audio_right = nullptr;

public:
  Synth synth;

  JackStandalone (jack_client_t *client) :
    client (client)
  {
    synth.set_sample_rate (jack_get_sample_rate (client));
    midi_input_port = jack_port_register (client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    audio_left = jack_port_register (client, "audio_out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    audio_right = jack_port_register (client, "audio_out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    jack_set_process_callback (client,
      [](jack_nframes_t nframes, void *arg)
        {
          auto self = static_cast<JackStandalone *> (arg);
          return self->process (nframes);
        }, this);
  }
  int
  process (jack_nframes_t nframes)
  {
    void* port_buf = jack_port_get_buffer (midi_input_port, nframes);
    jack_nframes_t event_count = jack_midi_get_event_count (port_buf);

    for (jack_nframes_t event_index = 0; event_index < event_count; event_index++)
      {
        jack_midi_event_t    in_event;
        jack_midi_event_get (&in_event, port_buf, event_index);
        synth.add_midi_event (in_event.time, in_event.buffer);
      }

    float *outputs[2] = {
      (float *) jack_port_get_buffer (audio_left, nframes),
      (float *) jack_port_get_buffer (audio_right, nframes)
    };
    return synth.process (outputs, nframes);
  }
  void
  run()
  {
    if (jack_activate (client))
      {
        fprintf (stderr, "cannot activate client");
        exit (1);
      }

    printf ("Synthesizer running - press \"Enter\" to quit.\n");
    getchar();
  }
};

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

  JackStandalone jack_standalone (client);

  if (!jack_standalone.synth.sfz_loader.parse (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  jack_standalone.run();

  jack_client_close (client);
  return 0;
}
