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

/* use private API (need to build fluidsynth without CMAKE_C_VISIBILITY_PRESET hidden) */
extern "C" {
  void fluid_voice_release (fluid_voice_t *voice);
};

using std::vector;
using std::string;
using std::regex;
using std::regex_replace;

SampleCache sample_cache;

struct SFZSynth
{
  std::minstd_rand random_gen;
  jack_port_t *midi_input_port = nullptr;
  jack_port_t *audio_left = nullptr;
  jack_port_t *audio_right = nullptr;
  fluid_synth_t *synth = nullptr;
  Loader sfz_loader;

  float
  env_time2gen (float time_sec)
  {
    return 1200 * log2f (std::clamp (time_sec, 0.001f, 100.f));
  }
  float
  env_level2gen (float level_perc)
  {
    return -10 * 20 * log10 (std::clamp (level_perc, 0.001f, 100.f) / 100);
  }
  struct Voice
  {
    bool used = false;
    int channel = 0;
    int key = 0;
    fluid_voice_t *flvoice = 0;
  };
  std::vector<Voice> voices;
  void
  add_voice (int channel, int key, fluid_voice_t *flvoice)
  {
    Voice *voice = nullptr;
    for (auto& v : voices)
      if (!v.used)
        {
          /* overwrite old voice in vector */
          voice = &v;
          break;
        }
    if (!voice)
      {
        voice = &voices.emplace_back();
      }
    voice->used = true;
    voice->channel = channel;
    voice->key = key;
    voice->flvoice = flvoice;
  }
  void
  note_on (int chan, int key, int vel)
  {
    // - random must be >= 0.0
    // - random must be <  1.0  (and never 1.0)
    double random = random_gen() / double (random_gen.max() + 1);

    for (auto& region : sfz_loader.regions)
      {
        if (region.lokey <= key && region.hikey >= key &&
            region.lovel <= vel && region.hivel >= vel &&
            region.trigger == Trigger::ATTACK)
          {
            bool cc_match = true;
            for (size_t cc = 0; cc < region.locc.size(); cc++)
              {
                if (region.locc[cc] != 0 || region.hicc[cc] != 127)
                  {
                    int val;
                    if (fluid_synth_get_cc (synth, chan, cc, &val) == FLUID_OK)
                      {
                        if (val < region.locc[cc] || val > region.hicc[cc])
                          cc_match = false;
                      }
                  }
              }
            if (!cc_match)
              continue;
            if (region.play_seq == region.seq_position)
              {
                /* in order to make sequences and random play nice together
                 * random check needs to be done inside sequence check
                 */
                if (region.lorand <= random && region.hirand > random)
                  {
                    if (region.cached_sample)
                      {
                        for (size_t ch = 0; ch < region.cached_sample->flsamples.size(); ch++)
                          {
                            auto flsample = region.cached_sample->flsamples[ch];

                            printf ("%d %s#%zd\n", key, region.sample.c_str(), ch);
                            if (region.loop_end)
                              {
                                fluid_sample_set_loop (flsample, region.loop_start, region.loop_end);
                              }
                            else if (region.cached_sample->loop)
                              {
                                fluid_sample_set_loop (flsample, region.cached_sample->loop_start, region.cached_sample->loop_end);
                              }
                            fluid_sample_set_pitch (flsample, region.pitch_keycenter, 0);

                            auto flvoice = fluid_synth_alloc_voice (synth, flsample, chan, key, vel);
                            fluid_voice_gen_set (flvoice, GEN_SAMPLEMODE, 1);

                            if (region.cached_sample->flsamples.size() == 2) /* pan stereo voices */
                              {
                                if (ch == 0)
                                  fluid_voice_gen_set (flvoice, GEN_PAN, -500);
                                else
                                  fluid_voice_gen_set (flvoice, GEN_PAN, 500);
                              }
                            /* volume envelope */
                            fluid_voice_gen_set (flvoice, GEN_VOLENVDELAY, env_time2gen (region.ampeg_delay));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVATTACK, env_time2gen (region.ampeg_attack));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVDECAY, env_time2gen (region.ampeg_decay));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVSUSTAIN, env_level2gen (region.ampeg_sustain));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVRELEASE, env_time2gen (region.ampeg_release));

                            fluid_synth_start_voice (synth, flvoice);
                            add_voice (chan, key, flvoice);
                          }
                      }
                  }
              }
            region.play_seq++;
            if (region.play_seq > region.seq_length)
              region.play_seq = 1;
          }
      }
  }
  void
  note_off (int chan, int key)
  {
    for (auto& voice : voices)
      {
        if (voice.used && voice.channel == chan && voice.key == key)
          {
            voice.used = false;
            fluid_voice_release (voice.flvoice);

            // we need to call this to ensure that the fluid synth instance can do some housekeeping jobs
            fluid_synth_noteoff (synth, 0, 0);
          }
      }
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
            unsigned char status  = in_event.buffer[0] & 0xf0;
            unsigned char channel = in_event.buffer[0] & 0x0f;
            if (status == 0x80)
              {
                // printf ("note off, ch = %d\n", channel);
                note_off (channel, in_event.buffer[1]);
              }
            else if (status == 0x90)
              {
                // printf ("note on, ch = %d, note = %d, vel = %d\n", channel, in_event.buffer[1], in_event.buffer[2]);
                note_on (channel, in_event.buffer[1], in_event.buffer[2]);
              }
          }
      }
    float *channel_values[2] = {
      (float *) jack_port_get_buffer (audio_left, nframes),
      (float *) jack_port_get_buffer (audio_right, nframes)
    };
    std::fill_n (channel_values[0], nframes, 0.0);
    std::fill_n (channel_values[1], nframes, 0.0);
    fluid_synth_process (synth, nframes,
                         0, nullptr, /* no effects */
                         2, channel_values);
    return 0;
  }
};

string
strip_spaces (const string& s)
{
  static const regex lws_re ("^\\s*");
  static const regex tws_re ("\\s*$");

  string r = regex_replace (s, tws_re, "");
  return regex_replace (r, lws_re, "");
}

bool
Loader::parse (const string& filename)
{
  sample_path = fs::path (filename).remove_filename();

  FILE *file = fopen (filename.c_str(), "r");
  assert (file);

  // read file
  vector<string> lines;

  string input;
  int ch = 0;
  while ((ch = fgetc (file)) >= 0)
    {
      if (ch != '\r' && ch != '\n')
        {
          input += ch;
        }
      else
        {
          if (ch == '\n')
            {
              lines.push_back (input);
              input = "";
            }
        }
    }
  if (!input.empty())
    lines.push_back (input);
  fclose (file);

  this->filename = filename;

  // strip comments
  static const regex comment_re ("//.*$");
  for (auto& l : lines)
    l = regex_replace (l, comment_re, "");

  static const regex space_re ("\\s+(.*)");
  static const regex tag_re ("<([^>]*)>(.*)");
  static const regex key_val_re ("([a-z0-9_]+)=(\\S+)(.*)");
  for (auto l : lines)
    {
      line_count++;
      while (l.size())
        {
          std::smatch sm;
          //printf ("@%s@\n", l.c_str());
          if (regex_match (l, sm, space_re))
            {
              l = sm[1];
            }
          else if (regex_match (l, sm, tag_re))
            {
              handle_tag (sm[1].str());
              l = sm[2];
            }
          else if (regex_match (l, sm, key_val_re))
            {
              string key = sm[1];
              string value = sm[2];
              if (key == "sample" || key == "sw_label")
                {
                  /* parsing sample=filename is problematic because filename can contain spaces
                   * we need to handle three cases:
                   *
                   * - filename extends until end of line
                   * - filename is followed by <tag>
                   * - filename is followed by key=value
                   */
                  static const regex key_val_space_re_eol ("[a-z0-9_]+=([^=<]+)");
                  static const regex key_val_space_re_tag ("[a-z0-9_]+=([^=<]+)(<.*)");
                  static const regex key_val_space_re_eq ("[a-z0-9_]+=([^=<]+)(\\s[a-z0-9_]+=.*)");
                  if (regex_match (l, sm, key_val_space_re_eol))
                    {
                      set_key_value (key, strip_spaces (sm[1].str()));
                      l = "";
                    }
                  else if (regex_match (l, sm, key_val_space_re_tag))
                    {
                      set_key_value (key, strip_spaces (sm[1].str()));
                      l = sm[2]; // parse rest
                    }
                  else if (regex_match (l, sm, key_val_space_re_eq))
                    {
                      set_key_value (key, strip_spaces (sm[1].str()));
                      l = sm[2]; // parse rest
                    }
                  else
                    {
                      fail ("sample/sw_label opcode");
                      return false;
                    }
                }
              else
                {
                  set_key_value (key, value);
                  l = sm[3];
                }
            }
          else
            {
              fail ("toplevel parsing");
              return false;
            }
        }
    }
  finish();
  printf ("*** regions: %zd\n", regions.size());
  return true;
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: liquidsfz <sfz_filename>");
      return 1;
    }
  jack_client_t *client = jack_client_open ("liquidsfz", JackNullOption, NULL);
  if (!client)
    {
       fprintf (stderr, "liquidsfz: unable to connect to jack server\n");
       exit (1);
     }
  SFZSynth sfz_synth;
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
        auto synth = static_cast<SFZSynth *> (arg);
        return synth->process (nframes);
      }, &sfz_synth);
  if (jack_activate (client))
    {
      fprintf (stderr, "cannot activate client");
      exit (1);
    }

  fluid_settings_t *settings = new_fluid_settings();
  fluid_settings_setnum (settings, "synth.sample-rate", jack_get_sample_rate (client));
  fluid_settings_setnum (settings, "synth.gain", 1.0);
  fluid_settings_setint (settings, "synth.reverb.active", 0);
  fluid_settings_setint (settings, "synth.chorus.active", 0);
  sfz_synth.synth = new_fluid_synth (settings);

  printf ("Synthesizer running - press \"Enter\" to quit.\n");
  getchar();

  delete_fluid_synth (sfz_synth.synth);
  delete_fluid_settings (settings);

  return 0;
}
