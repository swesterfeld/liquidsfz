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
#include <filesystem>
#include <random>
#include "samplecache.hh"

using std::vector;
using std::string;
using std::regex;
using std::regex_replace;
namespace fs = std::filesystem;

SampleCache sample_cache;

enum class Trigger {
  ATTACK,
  RELEASE,
  CC
};

struct Region
{
  string sample;
  SampleCache::Entry *cached_sample = nullptr;

  int lokey = 0;
  int hikey = 127;
  int lovel = 0;
  int hivel = 127;
  double lorand = 0;
  double hirand = 1;
  int pitch_keycenter = 60;
  int loop_start = 0;
  int loop_end = 0;
  Trigger trigger = Trigger::ATTACK;
  int seq_length = 1;
  int seq_position = 1;
  vector<int> locc = std::vector<int> (128, 0);
  vector<int> hicc = std::vector<int> (128, 127);
  float ampeg_delay = 0;
  float ampeg_attack = 0;
  float ampeg_decay = 0;
  float ampeg_sustain = 100;
  float ampeg_release = 0;

  bool empty()
  {
    return sample == "";
  }

  /* playback state */
  int play_seq = 1;
};

struct Loader
{
  int line_count = 0;
  string filename;
  enum class RegionType { NONE, GROUP, REGION };
  RegionType region_type = RegionType::NONE;
  Region active_group;
  Region active_region;
  vector<Region> regions;
  fs::path sample_path;

  int
  convert_key (const string& k)
  {
    if (k.size() >= 2)
      {
        int offset = -1;

        std::string::const_iterator ki = k.begin();
        switch (tolower (*ki++))
          {
            case 'c': offset = 0; break;
            case 'd': offset = 2; break;
            case 'e': offset = 4; break;
            case 'f': offset = 5; break;
            case 'g': offset = 7; break;
            case 'a': offset = 9; break;
            case 'b': offset = 11; break;
          }
        if (offset >= 0)
          {
            if (*ki == '#')
              {
                offset++;
                ki++;
              }
            else if (*ki == 'b')
              {
                offset--;
                ki++;
              }
            // c4 should be 60
            return atoi (std::string (ki, k.end()).c_str()) * 12 + offset + 12;
          }
      }
    return atoi (k.c_str());
  }
  int
  convert_int (const string& s)
  {
    return atoi (s.c_str());
  }
  float
  convert_float (const string& s)
  {
    return atof (s.c_str());
  }
  Trigger
  convert_trigger (const string& t)
  {
    if (t == "release")
      return Trigger::RELEASE;
    return Trigger::ATTACK;
  }
  bool
  starts_with (const string& key, const string& start)
  {
    return key.substr (0, start.size()) == start;
  }
  void
  set_key_value (const string& key, const string& value)
  {
    if (region_type == RegionType::NONE)
      return;

    Region& region = (region_type == RegionType::REGION) ? active_region : active_group;
    printf ("+++ '%s' = '%s'\n", key.c_str(), value.c_str());
    if (key == "sample")
      {
        // on unix, convert \-seperated filename to /-separated filename
        string native_filename = value;
        std::replace (native_filename.begin(), native_filename.end(), '\\', fs::path::preferred_separator);

        region.sample = fs::absolute (sample_path / native_filename);
      }
    else if (key == "lokey")
      region.lokey = convert_key (value);
    else if (key == "hikey")
      region.hikey = convert_key (value);
    else if (key == "key")
      {
        const int k = convert_key (value);
        region.lokey = k;
        region.hikey = k;
        region.pitch_keycenter = k;
      }
    else if (key == "lovel")
      region.lovel = convert_key (value);
    else if (key == "hivel")
      region.hivel = convert_key (value);
    else if (key == "pitch_keycenter")
      region.pitch_keycenter = convert_key (value);
    else if (key == "lorand")
      region.lorand = convert_float (value);
    else if (key == "hirand")
      region.hirand = convert_float (value);
    else if (key == "loop_start")
      region.loop_start = convert_int (value);
    else if (key == "loop_end")
      region.loop_end = convert_int (value);
    else if (starts_with (key, "locc"))
      {
        int cc = convert_int (key.substr (4));
        if (cc >= 0 && cc <= 127)
          region.locc[cc] = convert_int (value);
      }
    else if (starts_with (key, "hicc"))
      {
        int cc = convert_int (key.substr (4));
        if (cc >= 0 && cc <= 127)
          region.hicc[cc] = convert_int (value);
      }
    else if (starts_with (key, "on_locc") || starts_with (key, "on_hicc"))
      region.trigger = Trigger::CC;
    else if (key == "trigger")
      region.trigger = convert_trigger (value);
    else if (key == "seq_length")
      region.seq_length = convert_int (value);
    else if (key == "seq_position")
      region.seq_position = convert_int (value);
    else if (key == "ampeg_delay")
      region.ampeg_delay = convert_float (value);
    else if (key == "ampeg_attack")
      region.ampeg_attack = convert_float (value);
    else if (key == "ampeg_decay")
      region.ampeg_decay = convert_float (value);
    else if (key == "ampeg_sustain")
      region.ampeg_sustain = convert_float (value);
    else if (key == "ampeg_release")
      region.ampeg_release = convert_float (value);
    else
      printf ("unsupported opcode '%s'\n", key.c_str());
  }
  void
  handle_tag (const string& tag)
  {
    printf ("+++ TAG %s\n", tag.c_str());

    /* if we are done building a region, store it */
    if (tag == "region" || tag == "group")
      if (!active_region.empty())
        {
          regions.push_back (active_region);
          active_region = Region();
        }

    if (tag == "region")
      {
        region_type   = RegionType::REGION;
        active_region = active_group; /* inherit parameters from group */
      }
    else if (tag == "group")
      {
        region_type  = RegionType::GROUP;
        active_group = Region();
      }
    else
      printf ("unhandled tag '%s'\n", tag.c_str());
  }
  void
  finish()
  {
    if (!active_region.empty())
      regions.push_back (active_region);

    for (size_t i = 0; i < regions.size(); i++)
      {
        regions[i].cached_sample = sample_cache.load (regions[i].sample);
        printf ("loading %.1f %%\r", (i + 1) * 100.0 / regions.size());
        fflush (stdout);
      }
    printf ("\n");
  }
  void
  fail (const string& error)
  {
    fprintf (stderr, "parse error: %s: line %d: %s\n", filename.c_str(), line_count, error.c_str());
  }
  bool parse (const string& filename);
};

struct SFZSynth
{
  std::minstd_rand random_gen;
  fluid_preset_t *preset = nullptr;
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
  int
  note_on (fluid_synth_t *synth, int chan, int key, int vel)
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
                          }
                      }
                  }
              }
            region.play_seq++;
            if (region.play_seq > region.seq_length)
              region.play_seq = 1;
          }
      }
    return 0;
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
                fluid_synth_noteoff (synth, channel, in_event.buffer[1]);
              }
            else if (status == 0x90)
              {
                // printf ("note on, ch = %d, note = %d, vel = %d\n", channel, in_event.buffer[1], in_event.buffer[2]);
                note_on (synth, channel, in_event.buffer[1], in_event.buffer[2]);
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
              if (key == "sample")
                {
                  /* parsing sample=filename is problematic because filename can contain spaces
                   * we need to handle three cases:
                   *
                   * - filename extends until end of line
                   * - filename is followed by <tag>
                   * - filename is followed by key=value
                   */
                  static const regex sample_re_eol ("sample=([^=<]+)");
                  static const regex sample_re_tag ("sample=([^=<]+)(<.*)");
                  static const regex sample_re_eq ("sample=([^=<]+)(\\s[a-z0-9_]+=.*)");
                  if (regex_match (l, sm, sample_re_eol))
                    {
                      set_key_value ("sample", strip_spaces (sm[1].str()));
                      l = "";
                    }
                  else if (regex_match (l, sm, sample_re_tag))
                    {
                      set_key_value ("sample", strip_spaces (sm[1].str()));
                      l = sm[2]; // parse rest
                    }
                  else if (regex_match (l, sm, sample_re_eq))
                    {
                      set_key_value ("sample", strip_spaces (sm[1].str()));
                      l = sm[2]; // parse rest
                    }
                  else
                    {
                      fail ("sample opcode");
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
