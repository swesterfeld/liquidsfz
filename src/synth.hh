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

#include <random>
#include <algorithm>
#include <assert.h>

#include "loader.hh"
#include "utils.hh"

class Synth
{
  static constexpr double VOLUME_HEADROOM_DB = 12;

  std::minstd_rand random_gen;
#if 0 /* FIXME */
  fluid_synth_t *synth = nullptr;
  fluid_settings_t *settings = nullptr;
  fluid_sfont_t *fake_sfont = nullptr;
  fluid_preset_t *fake_preset = nullptr;
#endif
  unsigned int next_id = 0;

public:
  Loader sfz_loader;

  Synth (uint sample_rate)
  {
#if 0
    settings = new_fluid_settings();
    fluid_settings_setnum (settings, "synth.sample-rate", sample_rate);
    fluid_settings_setnum (settings, "synth.gain", db_to_factor (Synth::VOLUME_HEADROOM_DB));
    fluid_settings_setint (settings, "synth.reverb.active", 0);
    fluid_settings_setint (settings, "synth.chorus.active", 0);
    synth = new_fluid_synth (settings);
#endif /* FIXME */
  }
  ~Synth()
  {
#if 0
    delete_fluid_synth (synth);
    delete_fluid_settings (settings);
#endif
  }

#if 0
  fluid_voice_t *
  alloc_voice_with_id (fluid_sample_t *flsample, int chan, int key, int vel)
  {
    struct Data {
      fluid_sample_t *flsample = nullptr;
      fluid_voice_t *flvoice = nullptr;
    } data;
    if (!fake_preset)
      {
        fake_sfont = new_fluid_sfont (
          [](fluid_sfont_t *) { return ""; },
          [](fluid_sfont_t *sfont, int bank, int prenum) { return (fluid_preset_t *) nullptr; },
          nullptr, nullptr,
          [](fluid_sfont_t *) { return int(0); });

        auto note_on_lambda = [](fluid_preset_t *preset, fluid_synth_t *synth, int chan, int key, int vel)
          {
            Data *data = (Data *) fluid_preset_get_data (preset);
            data->flvoice = fluid_synth_alloc_voice (synth, data->flsample, chan, key, vel);
            return 0;
          };
        fake_preset = new_fluid_preset (fake_sfont,
          [](fluid_preset_t *) { return "preset"; },
          [](fluid_preset_t *) { return 0; },
          [](fluid_preset_t *) { return 0; },
          note_on_lambda,
          [](fluid_preset_t *) { });
      }
    data.flsample = flsample;
    fluid_preset_set_data (fake_preset, &data);
    fluid_synth_start (synth, next_id++, fake_preset, 0, chan, key, vel);
    return data.flvoice;
  }
#endif /* FIXME */
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
  double
  pan_stereo_db (const Region& r, int ch)
  {
    /* sine panning law: we use this because fluidsynth also uses the same law
     * for panning mono voices (and we want identical stereo/mono panning)
     */

    const double pan = ch == 0 ? -r.pan : r.pan;
    return db_from_factor (sin ((pan + 100) / 400 * M_PI), -144);
  }
  double
  velocity_track_db (const Region& r, int midi_velocity)
  {
    double curve = (midi_velocity * midi_velocity) / (127.0 * 127.0);
    double veltrack_factor = r.amp_veltrack * 0.01;

    double offset = (veltrack_factor >= 0) ? 1 : 0;
    double v = (offset - veltrack_factor) + veltrack_factor * curve;

    return db_from_factor (v, -144);
  }
  struct Voice
  {
    int channel = 0;
    int key = 0;
    bool used = false;
    double ppos = 0;

    const Region *region = nullptr;
  };
  std::vector<Voice> voices;
  void
  add_voice (const Region& region, int channel, int key, int velocity)
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
    voice->region = &region;
    voice->channel = channel;
    voice->key = key;
    voice->ppos = 0;
    voice->used = true;
    printf ("new voice %s\n", region.sample.c_str());
  }
#if 0
  void
  cleanup_finished_voice (fluid_voice_t *flvoice)
  {
    for (auto& voice : voices)
      {
        if (flvoice == voice.flvoice)
          voice.flvoice = nullptr;
      }
  }
#endif /* FIXME */
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
#if 0
                    int val;
                    if (fluid_synth_get_cc (synth, chan, cc, &val) == FLUID_OK)
                      {
                        if (val < region.locc[cc] || val > region.hicc[cc])
                          cc_match = false;
                      }
#endif /* FIXME */
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
                        add_voice (region, chan, key, vel);
#if 0
                        for (size_t ch = 0; ch < region.cached_sample->flsamples.size(); ch++)
                          {
                            auto flsample = region.cached_sample->flsamples[ch];

                            printf ("%d %s#%zd\n", key, region.sample.c_str(), ch);
                            fluid_sample_set_pitch (flsample, region.pitch_keycenter, 0);

                            // auto flvoice = fluid_synth_alloc_voice (synth, flsample, chan, key, vel);
                            /* note: we handle velocity based volume (amp_veltrack) ourselves:
                             *    -> we play all notes via fluidsynth at maximum velocity */
                            auto flvoice = alloc_voice_with_id (flsample, chan, key, 127);
                            cleanup_finished_voice (flvoice);

                            if (region.loop_mode == LoopMode::SUSTAIN || region.loop_mode == LoopMode::CONTINUOUS)
                              {
                                fluid_sample_set_loop (flsample, region.loop_start, region.loop_end);
                                fluid_voice_gen_set (flvoice, GEN_SAMPLEMODE, 1);
                              }

                            double pan_vol_db = 0;
                            if (region.cached_sample->flsamples.size() == 2) /* pan stereo voices */
                              {
                                pan_vol_db = pan_stereo_db (region, ch);
                                if (ch == 0)
                                  fluid_voice_gen_set (flvoice, GEN_PAN, -500);
                                else
                                  fluid_voice_gen_set (flvoice, GEN_PAN, 500);
                              }
                            else
                              {
                                fluid_voice_gen_set (flvoice, GEN_PAN, 5 * region.pan);
                              }
                            /* volume envelope */
                            fluid_voice_gen_set (flvoice, GEN_VOLENVDELAY, env_time2gen (region.ampeg_delay));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVATTACK, env_time2gen (region.ampeg_attack));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVDECAY, env_time2gen (region.ampeg_decay));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVSUSTAIN, env_level2gen (region.ampeg_sustain));
                            fluid_voice_gen_set (flvoice, GEN_VOLENVRELEASE, env_time2gen (region.ampeg_release));

                            /* volume */
                            double attenuation = -10 * (region.volume - VOLUME_HEADROOM_DB + velocity_track_db (region, vel) + pan_vol_db);
                            fluid_voice_gen_set (flvoice, GEN_ATTENUATION, attenuation);

                            fluid_synth_start_voice (synth, flvoice);
                            add_voice (&region, chan, key, flvoice);
                          }
#endif /* FIXME */
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
#if 0
    for (auto& voice : voices)
      {
        if (voice.flvoice && voice.channel == chan && voice.key == key && voice.region->loop_mode != LoopMode::ONE_SHOT)
          {
            // fluid_synth_release_voice (synth, voice.flvoice);
            fluid_synth_stop (synth, voice.id);
            voice.flvoice = nullptr;
          }
      }
#endif /* FIXME */
  }
  struct
  MidiEvent
  {
    uint offset;
    unsigned char midi_data[3];
  };
  std::vector<MidiEvent> midi_events;
  void
  add_midi_event (uint offset, unsigned char *midi_data)
  {
    unsigned char status = midi_data[0] & 0xf0;
    if (status == 0x80 || status == 0x90)
      {
        MidiEvent event;
        event.offset = offset;
        std::copy_n (midi_data, 3, event.midi_data);
        midi_events.push_back (event);
      }
  }
  int
  process (float **outputs, uint nframes)
  {
    for (const auto& midi_event : midi_events)
      {
        unsigned char status  = midi_event.midi_data[0] & 0xf0;
        unsigned char channel = midi_event.midi_data[0] & 0x0f;
        if (status == 0x80)
          {
            // printf ("note off, ch = %d\n", channel);
            note_off (channel, midi_event.midi_data[1]);
          }
        else if (status == 0x90)
          {
            // printf ("note on, ch = %d, note = %d, vel = %d\n", channel, in_event.buffer[1], in_event.buffer[2]);
            note_on (channel, midi_event.midi_data[1], midi_event.midi_data[2]);
          }
      }
    std::fill_n (outputs[0], nframes, 0.0);
    std::fill_n (outputs[1], nframes, 0.0);
    for (auto& voice : voices)
      {
        if (voice.used)
          {
            const auto csample = voice.region->cached_sample;
            const auto channels = csample->channels;

            for (uint i = 0; i < nframes; i++)
              {
                uint ii = voice.ppos;
                uint x = ii * channels;
                for (uint c = 0; c < csample->channels; c++)
                  {
                    if (x < csample->samples.size())
                      outputs[c][i] += csample->samples[x] * (1 / 32768.);
                  }
                voice.ppos += 1;
              }
          }
      }
    midi_events.clear();
    return 0;
  }
};


