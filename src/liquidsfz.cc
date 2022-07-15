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
#include <filesystem>
#include <readline/readline.h>
#include <readline/history.h>
#include "loader.hh"
#include "utils.hh"
#include "synth.hh"
#include "liquidsfz.hh"
#include "cliparser.hh"
#include "argparser.hh"
#include "config.h"

#include <term.h>
#include <sys/resource.h>

using std::vector;
using std::string;

using namespace LiquidSFZ;

using LiquidSFZInternal::path_join;
using LiquidSFZInternal::ArgParser;
using LiquidSFZInternal::string_printf;
using LiquidSFZInternal::get_time;

namespace Options
{
  bool debug = false;
  int  quality = -1;
  int  preload_time = -1;
}

class CommandQueue
{
  struct Command
  {
    std::function<void()> fun;
  };
  vector<Command> commands;
  bool            done = false;
  std::mutex      mutex;
public:
  void
  append (std::function<void()> fun) // main thread
  {
    std::lock_guard lg (mutex);

    /* we insert and free in this thread to avoid malloc() in audio thread */
    if (done)
      {
        commands.clear();
        done = false;
      }

    Command c;
    c.fun = fun;
    commands.push_back (c);
  }
  void
  run() // audio thread
  {
    if (mutex.try_lock()) /* never stall audio thread */
      {
        if (!done)
          {
            for (auto& c : commands)
              c.fun();

            done = true;
          }
        mutex.unlock();
      }
  }
  void
  wait_all() // main thread
  {
    for (;;)
      {
        usleep (10 * 1000);
        std::lock_guard lg (mutex);
        if (commands.empty() || done)
          return;
      }
  }
};

string
liquidsfz_user_data_dir()
{
  string dir;
  const char *data_dir_env = getenv ("XDG_DATA_HOME");
  if (data_dir_env && data_dir_env[0])
    {
      dir = data_dir_env;
    }
  else
    {
      const char *home = getenv ("HOME");
      if (home && home[0])
        {
          dir = path_join (path_join (home, ".local"), "share");
        }
    }
  if (!dir.empty())
    {
      dir = path_join (dir, "liquidsfz");
      std::filesystem::create_directories (dir);
    }

  return dir;
}

class JackStandalone
{
  jack_client_t *client = nullptr;
  jack_port_t *midi_input_port = nullptr;
  jack_port_t *audio_left = nullptr;
  jack_port_t *audio_right = nullptr;

  CommandQueue cmd_q;
  Synth synth;
  std::mutex synth_mutex;
  std::vector<KeyInfo> keys;
  std::vector<CCInfo> ccs;

public:
  JackStandalone (jack_client_t *client) :
    client (client)
  {
    if (Options::debug)
      synth.set_log_level (Log::DEBUG);
    if (Options::quality > 0)
      synth.set_sample_quality (Options::quality);
    if (Options::preload_time >= 0)
      synth.set_preload_time (Options::preload_time);

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
  process (jack_nframes_t n_frames)
  {
    cmd_q.run(); // execute pending commands

    float *outputs[2] = {
      (float *) jack_port_get_buffer (audio_left, n_frames),
      (float *) jack_port_get_buffer (audio_right, n_frames)
    };
    if (!synth_mutex.try_lock())
      {
        // synth is in use (probably loading an sfz file), so we cannot call process() here
        std::fill_n (outputs[0], n_frames, 0.0);
        std::fill_n (outputs[1], n_frames, 0.0);
        return 0;
      }

    void* port_buf = jack_port_get_buffer (midi_input_port, n_frames);
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
                case 0xe0: synth.add_event_pitch_bend (in_event.time, channel, in_event.buffer[1] + 128 * in_event.buffer[2]);
                           break;
              }
          }
      }

    synth.process (outputs, n_frames);
    synth_mutex.unlock();
    return 0;
  }
  void
  run()
  {
    show_ccs();
    if (jack_activate (client))
      {
        fprintf (stderr, "cannot activate client");
        exit (1);
      }

    string history_file = path_join (liquidsfz_user_data_dir(), "history");
    read_history (history_file.c_str());

    printf ("Type 'quit' to quit, 'help' for help.\n");

    bool is_running = true;
    string last_input;
    while (is_running)
      {
        char *input_c = readline ("liquidsfz> ");
        if (!input_c)
          {
            printf ("\n");
            break;
          }
        string input = input_c;
        free (input_c);

        for (unsigned char ch : input)
          {
            /* do not add pure whitespace lines to history */
            if (ch > 32)
              {
                /* do not add identical lines */
                if (last_input != input)
                  {
                    add_history (input.c_str());
                    last_input = input;
                    break;
                  }
              }
          }
        is_running = execute (input);
      }
    write_history (history_file.c_str());
    history_truncate_file (history_file.c_str(), 500);
  }
  bool
  execute (const string& input)
  {
    CLIParser cli_parser;
    cli_parser.parse (input);

    int ch, key, vel, cc, value;
    double dvalue;
    string script, text, filename;
    if (cli_parser.empty_line())
      {
        /* empty line (or comment) */
      }
    else if (cli_parser.command ("quit"))
      {
        return false;
      }
    else if (cli_parser.command ("help"))
      {
        printf ("help                - show this help\n");
        printf ("quit                - quit liquidsfz\n");
        printf ("\n");
        printf ("load sfz_filename   - load sfz from filename\n");
        printf ("allsoundoff         - stop all sounds\n");
        printf ("reset               - system reset (stop all sounds, reset controllers)\n");
        printf ("noteon chan key vel - start note\n");
        printf ("noteoff chan key    - stop note\n");
        printf ("cc chan ctrl value  - send controller event\n");
        printf ("pitch_bend chan val - send pitch bend event (0 <= val <= 16383)\n");
        printf ("gain value          - set gain (0 <= value <= 5)\n");
        printf ("max_voices value    - set maximum number of voices\n");
        printf ("max_cache_size size - set maximum cache size in MB\n");
        printf ("preload_time time   - set preload time in ms\n");
        printf ("keys                - show keys supported by the sfz\n");
        printf ("switches            - show switches supported by the sfz\n");
        printf ("ccs                 - show ccs supported by the sfz\n");
        printf ("stats               - show voices/cache/cpu usage\n");
        printf ("info                - show information\n");
        printf ("voice_count         - print number of active synthesis voices\n");
        printf ("sleep time_ms       - sleep for some milliseconds\n");
        printf ("source filename     - load a file and execute each line as command\n");
        printf ("echo text           - print text\n");
      }
    else if (cli_parser.command ("load", filename))
      {
        std::lock_guard lg (synth_mutex); // can't process() while loading
        if (load (filename))
          printf ("ok\n");
        else
          printf ("failed\n");
      }
    else if (cli_parser.command ("allsoundoff"))
      {
        cmd_q.append ([=] () { synth.all_sound_off(); });
      }
    else if (cli_parser.command ("reset"))
      {
        cmd_q.append ([=] () { synth.system_reset(); });
      }
    else if (cli_parser.command ("noteon", ch, key, vel))
      {
        cmd_q.append ([=]() { synth.add_event_note_on (0, ch, key, vel); });
      }
    else if (cli_parser.command ("noteoff", ch, key))
      {
        cmd_q.append ([=]() { synth.add_event_note_off (0, ch, key); });
      }
    else if (cli_parser.command ("cc", ch, cc, value))
      {
        cmd_q.append ([=]() { synth.add_event_cc (0, ch, cc, value); });
      }
    else if (cli_parser.command ("pitch_bend", ch, value))
      {
        cmd_q.append ([=]() { synth.add_event_pitch_bend (0, ch, value); });
      }
    else if (cli_parser.command ("gain", dvalue))
      {
        cmd_q.append ([=]() { synth.set_gain (std::clamp (dvalue, 0., 5.)); });
      }
    else if (cli_parser.command ("max_voices", value))
      {
        // NOTE: this is not RT safe so we have to use the lock here
        std::lock_guard lg (synth_mutex);

        synth.set_max_voices (std::clamp (value, 0, 4096));
      }
    else if (cli_parser.command ("max_cache_size", dvalue))
      {
        // atomic (no synchronization necessary)
        synth.set_max_cache_size (std::clamp<double> (dvalue, 0, 256 * 1024) * 1024 * 1024);
      }
    else if (cli_parser.command ("max_cache_size"))
      {
        // atomic (no synchronization necessary)
        printf ("Maximum cache size: %.1f MB\n", synth.max_cache_size() / 1024. / 1024.);
      }
    else if (cli_parser.command ("preload_time", value))
      {
        cmd_q.append ([=]() { synth.set_preload_time (value); });
      }
    else if (cli_parser.command ("keys"))
      {
        show_keys (false);
      }
    else if (cli_parser.command ("switches"))
      {
        show_keys (true);
      }
    else if (cli_parser.command ("ccs"))
      {
        show_ccs();
      }
    else if (cli_parser.command ("voice_count"))
      {
        int v = -1;
        cmd_q.append ([&v, this]() { v = synth.active_voice_count(); });
        cmd_q.wait_all();
        printf ("%d\n", v);
      }
    else if (cli_parser.command ("stats"))
      {
        show_stats();
      }
    else if (cli_parser.command ("info"))
      {
        show_info();
      }
    else if (cli_parser.command ("sleep", dvalue))
      {
        usleep (lrint (dvalue * 1000));
      }
    else if (cli_parser.command ("source", script))
      {
        FILE *f = fopen (script.c_str(), "r");
        if (f)
          {
            char buf[1024];
            while (fgets (buf, sizeof (buf), f))
              {
                if (!execute (buf))
                  break;
              }
            fclose (f);
          }
      }
    else if (cli_parser.command ("echo", text))
      {
        printf ("%s\n", text.c_str());
      }
    else
      {
        printf ("error while parsing command\n");
      }
    return true;
  }
  bool
  load (const string& filename)
  {
    synth.set_progress_function ([] (double percent)
      {
        printf ("Loading: %.1f %%\r", percent);
        fflush (stdout);
      });

    bool load_ok = synth.load (filename);
    if (!load_ok)
      return false;

    keys = synth.list_keys();
    ccs = synth.list_ccs();

    printf ("%30s\r", ""); // overwrite progress message
    printf ("Preloaded %d samples, %.1f MB.\n\n", synth.cache_file_count(), synth.cache_size() / 1024. / 1024.);
    return true;
  }
  void
  show_keys (bool is_switch)
  {
    for (const auto& k : keys)
      {
        string label = k.label();
        if (label == "")
          label = "-";

        if (is_switch == k.is_switch())
          printf ("%d %s\n", k.key(), label.c_str());
      }
  }
  void
  show_ccs()
  {
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
  }
  void
  show_info()
  {
    int voices = -1;
    int max_voices = -1;
    size_t cache_size = 0;
    size_t max_cache_size = 0;
    int cache_file_count = 0;
    int preload_time_ms = 0;
    int sample_rate = 0;
    int sample_quality = 0;

    cmd_q.append ([&]() {
      voices = synth.active_voice_count();
      max_voices = synth.max_voices();
      cache_size = synth.cache_size();
      max_cache_size = synth.max_cache_size();
      cache_file_count = synth.cache_file_count();
      preload_time_ms = synth.preload_time();
      sample_rate = synth.sample_rate();
      sample_quality = synth.sample_quality();
    });
    cmd_q.wait_all();

    string sample_quality_str;
    switch (sample_quality)
      {
        case 1: sample_quality_str = "low";
                break;
        case 2: sample_quality_str = "medium";
                break;
        case 3: sample_quality_str = "high";
                break;
      }
    printf ("Active Voices            : %d\n", voices);
    printf ("Maximum Number of Voices : %d\n", max_voices);
    printf ("Sample Quality           : %d (%s)\n", sample_quality, sample_quality_str.c_str());
    printf ("Preload Time             : %d ms\n", preload_time_ms);
    printf ("Cached Samples           : %d\n", cache_file_count);
    printf ("Cache Size               : %.1f MB\n", cache_size / 1024. / 1024.);
    printf ("Maximum Cache Size       : %.1f MB\n", max_cache_size / 1024. / 1024.);
    printf ("Sample Rate              : %d\n", sample_rate);
  }
  void
  show_stats()
  {
    termios tio_orig;

    tcgetattr (STDIN_FILENO, &tio_orig);

    termios tio_new = tio_orig;
    tio_new.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
    tio_new.c_cc[VMIN] = 0;
    tio_new.c_cc[VTIME] = 1; /* 0.1 seconds */
    tcsetattr (STDIN_FILENO, TCSANOW, &tio_new);

    double last_rusage_time = 0;
    double last_time = 0;
    double cpu_usage = 0;
    string old_stats;
    while (1)
      {
        int voices = -1;
        int max_voices = -1;
        size_t cache_size = 0;
        int cache_file_count = 0;

        cmd_q.append ([&]() {
          voices = synth.active_voice_count();
          max_voices = synth.max_voices();
          cache_size = synth.cache_size();
          cache_file_count = synth.cache_file_count();
        });
        cmd_q.wait_all();

        double time = get_time();
        if (fabs (time - last_time) > 2)
          {
            rusage ru;
            getrusage (RUSAGE_SELF, &ru);
            auto tv_to_sec = [] (auto tv) { return tv.tv_sec + tv.tv_usec / 1000000.0; };
            double rusage_time = tv_to_sec (ru.ru_utime) + tv_to_sec (ru.ru_stime);
            cpu_usage = (rusage_time - last_rusage_time) / (time - last_time) * 100;
            last_time = time;
            last_rusage_time = rusage_time;
          }

        string stats = string_printf ("Voices: %d/%d ~ Cache: %d samples | %.1f MB ~ CPU: %.1f%%", voices, max_voices, cache_file_count, cache_size / 1024. / 1024., cpu_usage);
        if (stats != old_stats)
          {
            printf ("%s     \r", stats.c_str());
            fflush (stdout);

            old_stats = stats;
          }
        char buffer[1024];
        int r = read (0, buffer, sizeof (buffer));
        if (r > 0)
          break;
      }
    printf ("\n");
    tcsetattr (STDIN_FILENO, TCSANOW, &tio_orig);
  }
};

static void
print_usage()
{
  printf ("usage: liquidsfz [options] <sfz_file>\n");
  printf ("\n");
  printf ("Options:\n");
  printf ("  --debug         enable debugging output\n");
  printf ("  --quality       set sample playback quality (1-3) [3]\n");
  printf ("  --preload-time  set sample preload time in milliseconds [500]\n");
}

int
main (int argc, char **argv)
{
  ArgParser ap (argc, argv);

  if (ap.parse_opt ("--help") || ap.parse_opt ("-h"))
    {
      print_usage();
      return 0;
    }
  if (ap.parse_opt ("--version") || ap.parse_opt ("-v"))
    {
      printf ("liquidsfz %s\n", VERSION);
      return 0;
    }
  if (ap.parse_opt ("--debug"))
    {
      Options::debug = true;
    }
  ap.parse_opt ("--quality", Options::quality);
  ap.parse_opt ("--preload-time", Options::preload_time);

  vector<string> args;
  if (!ap.parse_args (1, args))
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

  if (!jack_standalone.load (args[0]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  jack_standalone.run();

  jack_client_close (client);
  return 0;
}
