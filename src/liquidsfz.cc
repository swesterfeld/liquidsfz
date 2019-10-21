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

using std::vector;
using std::string;
using std::regex;
using std::regex_replace;

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
    int channel = 0;
    int key = 0;
    int id = -1;
    fluid_voice_t *flvoice = nullptr;
    Region *region = nullptr;
  };
  std::vector<Voice> voices;
  void
  add_voice (Region *region, int channel, int key, fluid_voice_t *flvoice)
  {
    assert (flvoice);

    Voice *voice = nullptr;
    for (auto& v : voices)
      if (!v.flvoice)
        {
          /* overwrite old voice in vector */
          voice = &v;
          break;
        }
    if (!voice)
      {
        voice = &voices.emplace_back();
      }
    voice->region = region;
    voice->channel = channel;
    voice->key = key;
    voice->id = fluid_voice_get_id (flvoice);
    voice->flvoice = flvoice;
  }
  void
  cleanup_finished_voice (fluid_voice_t *flvoice)
  {
    for (auto& voice : voices)
      {
        if (flvoice == voice.flvoice)
          voice.flvoice = nullptr;
      }
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
                            fluid_sample_set_pitch (flsample, region.pitch_keycenter, 0);

                            auto flvoice = fluid_synth_alloc_voice (synth, flsample, chan, key, vel);
                            cleanup_finished_voice (flvoice);

                            if (region.loop_mode == LoopMode::SUSTAIN || region.loop_mode == LoopMode::CONTINUOUS)
                              {
                                fluid_sample_set_loop (flsample, region.loop_start, region.loop_end);
                                fluid_voice_gen_set (flvoice, GEN_SAMPLEMODE, 1);
                              }

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
                            add_voice (&region, chan, key, flvoice);
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
        if (voice.flvoice && voice.channel == chan && voice.key == key && voice.region->loop_mode != LoopMode::ONE_SHOT)
          {
            // fluid_synth_stop (synth, voice.id));
            fluid_synth_release_voice (synth, voice.flvoice);
            voice.flvoice = nullptr;
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
