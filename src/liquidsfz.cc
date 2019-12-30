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
#include "liquidsfz.hh"
#include "config.h"

using std::vector;
using std::string;

using namespace LiquidSFZ;

namespace Options
{
  bool debug = false;
}

static void
print_usage()
{
  printf ("usage: liquidsfz [options] <sfz_file>\n");
  printf ("\n");
  printf ("Options:\n");
  printf ("  --debug      enable debugging output\n");
}

static bool
check_arg (uint         argc,
           char        *argv[],
           uint        *nth,
           const char  *opt,		    /* for example: --foo */
           const char **opt_arg = nullptr)  /* if foo needs an argument, pass a pointer to get the argument */
{
  assert (opt != nullptr);
  assert (*nth < argc);

  const char *arg = argv[*nth];
  if (!arg)
    return false;

  uint opt_len = strlen (opt);
  if (strcmp (arg, opt) == 0)
    {
      if (opt_arg && *nth + 1 < argc)	  /* match foo option with argument: --foo bar */
        {
          argv[(*nth)++] = nullptr;
          *opt_arg = argv[*nth];
          argv[*nth] = nullptr;
          return true;
        }
      else if (!opt_arg)		  /* match foo option without argument: --foo */
        {
          argv[*nth] = nullptr;
          return true;
        }
      /* fall through to error message */
    }
  else if (strncmp (arg, opt, opt_len) == 0 && arg[opt_len] == '=')
    {
      if (opt_arg)			  /* match foo option with argument: --foo=bar */
        {
          *opt_arg = arg + opt_len + 1;
          argv[*nth] = nullptr;
          return true;
        }
      /* fall through to error message */
    }
  else
    return false;

  print_usage();
  exit (1);
}

void
parse_options (int   *argc_p,
               char **argv_p[])
{
  uint argc = *argc_p;
  char **argv = *argv_p;
  unsigned int i, e;

  for (i = 1; i < argc; i++)
    {
      //const char *opt_arg;
      if (strcmp (argv[i], "--help") == 0 ||
          strcmp (argv[i], "-h") == 0)
	{
	  print_usage();
	  exit (0);
	}
      else if (strcmp (argv[i], "--version") == 0 || strcmp (argv[i], "-v") == 0)
	{
	  printf ("liquidsfz %s\n", VERSION);
	  exit (0);
	}
      else if (check_arg (argc, argv, &i, "--debug"))
	{
          Options::debug = true;
	}
    }

  /* resort argc/argv */
  e = 1;
  for (i = 1; i < argc; i++)
    if (argv[i])
      {
        argv[e++] = argv[i];
        if (i >= e)
          argv[i] = nullptr;
      }
  *argc_p = e;
}

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
    if (Options::debug)
      synth.set_log_level (Log::DEBUG);

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
        if (in_event.size == 3)
          {
            int channel = in_event.buffer[0] & 0x0f;
            switch (in_event.buffer[0] & 0xf0)
              {
                case 0x90: synth.add_event_note_on (in_event.time, channel, in_event.buffer[1], in_event.buffer[2]);
                           break;
                case 0x80: synth.add_event_note_off (in_event.time, channel, in_event.buffer[1]);
                           break;
                case 0xb0: synth.add_event_cc (in_event.time, channel, in_event.buffer[1], in_event.buffer[2]);
                           break;
              }
          }
      }

    float *outputs[2] = {
      (float *) jack_port_get_buffer (audio_left, nframes),
      (float *) jack_port_get_buffer (audio_right, nframes)
    };
    synth.process (outputs, nframes);
    return 0;
  }
  void
  run()
  {
    auto ccs = synth.list_ccs();
    if (ccs.size())
      {
        printf ("Supported Controls:\n");
        for (const auto& cc_info : ccs)
          {
            printf (" - CC #%d", cc_info.cc());
            if (cc_info.has_label())
              printf (" - %s", cc_info.label().c_str());
            printf (" [ default %d ]\n", cc_info.default_value());
          }
        printf ("\n");
      }
    if (jack_activate (client))
      {
        fprintf (stderr, "cannot activate client");
        exit (1);
      }

    printf ("Synthesizer running - press \"Enter\" to quit.\n");
    getchar();
  }
  bool
  load (const string& filename)
  {
    synth.set_progress_function ([] (double percent)
      {
        printf ("Loading: %.1f %%\r", percent);
        fflush (stdout);
      });

    return synth.load (filename);
  }
};

int
main (int argc, char **argv)
{
  parse_options (&argc, &argv);
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

  if (!jack_standalone.load (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  jack_standalone.run();

  jack_client_close (client);
  return 0;
}
