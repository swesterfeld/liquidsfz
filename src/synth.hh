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
  std::minstd_rand random_gen;
  uint sample_rate_ = 44100; // default

public:
  Loader sfz_loader;

  void
  set_sample_rate (uint sample_rate)
  {
    sample_rate_ = sample_rate;
  }
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
  pan_stereo_factor (const Region& r, int ch)
  {
    /* sine panning law: we use this because fluidsynth also uses the same law
     * for panning mono voices (and we want identical stereo/mono panning)
     */

    const double pan = ch == 0 ? -r.pan : r.pan;
    return sin ((pan + 100) / 400 * M_PI);
  }
  double
  velocity_track_factor (const Region& r, int midi_velocity)
  {
    double curve = (midi_velocity * midi_velocity) / (127.0 * 127.0);
    double veltrack_factor = r.amp_veltrack * 0.01;

    double offset = (veltrack_factor >= 0) ? 1 : 0;
    double v = (offset - veltrack_factor) + veltrack_factor * curve;

    return v;
  }
  struct Voice
  {
    int channel = 0;
    int key = 0;
    bool used = false;
    double ppos = 0;
    float left_gain = 0;
    float right_gain = 0;

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
    double volume_gain = db_to_factor (region.volume);
    double velocity_gain = velocity_track_factor (region, velocity);
    voice->left_gain = velocity_gain * volume_gain * pan_stereo_factor (region, 0);
    voice->right_gain = velocity_gain * volume_gain * pan_stereo_factor (region, 1);
    voice->used = true;
    printf ("new voice %s - channels %d\n", region.sample.c_str(), region.cached_sample->channels);
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
    for (auto& voice : voices)
      {
        if (voice.used && voice.channel == chan && voice.key == key && voice.region->loop_mode != LoopMode::ONE_SHOT)
          {
            voice.used = false; // FIXME: envelope
          }
      }
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
  static double
  note_to_freq (int note)
  {
    return 440 * exp (log (2) * (note - 69) / 12.0);
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
            const double step = double (csample->sample_rate) / sample_rate_ * note_to_freq (voice.key) / note_to_freq (voice.region->pitch_keycenter);

            auto get_samples_mono = [csample, &voice] (uint x, auto& fsamples)
              {
                for (uint i = 0; i < fsamples.size(); i++)
                  {
                    if (x >= csample->samples.size())
                      {
                        fsamples[i] = 0;
                      }
                    else
                      {
                        fsamples[i] = csample->samples[x];
                        x++;

                        if (voice.region->loop_mode == LoopMode::SUSTAIN || voice.region->loop_mode == LoopMode::CONTINUOUS)
                          if (x > uint (voice.region->loop_end))
                            x = voice.region->loop_start;
                      }
                  }
              };

            auto get_samples_stereo = [csample, &voice] (uint x, auto& fsamples)
              {
                for (uint i = 0; i < fsamples.size(); i += 2)
                  {
                    if (x >= csample->samples.size())
                      {
                        fsamples[i]     = 0;
                        fsamples[i + 1] = 0;
                      }
                    else
                      {
                        fsamples[i]     = csample->samples[x];
                        fsamples[i + 1] = csample->samples[x + 1];
                        x += 2;

                        if (voice.region->loop_mode == LoopMode::SUSTAIN || voice.region->loop_mode == LoopMode::CONTINUOUS)
                          if (x > uint (voice.region->loop_end * 2))
                            x = voice.region->loop_start * 2;
                      }
                  }
              };

            for (uint i = 0; i < nframes; i++)
              {
                const uint ii = voice.ppos;
                const uint x = ii * channels;
                const float frac = voice.ppos - ii;
                if (x < csample->samples.size())
                  {
                    if (channels == 1)
                      {
                        std::array<float, 2> fsamples;
                        get_samples_mono (x, fsamples);

                        const float interp = fsamples[0] * (1 - frac) + fsamples[1] * frac;
                        outputs[0][i] += interp * voice.left_gain * (1 / 32768.);
                        outputs[1][i] += interp * voice.right_gain * (1 / 32768.);
                      }
                    else if (channels == 2)
                      {
                        std::array<float, 4> fsamples;
                        get_samples_stereo (x, fsamples);

                        outputs[0][i] += (fsamples[0] * (1 - frac) + fsamples[2] * frac) * voice.left_gain * (1 / 32768.);
                        outputs[1][i] += (fsamples[1] * (1 - frac) + fsamples[3] * frac) * voice.right_gain * (1 / 32768.);
                      }
                    else
                      {
                        assert (false);
                      }
                  }
                else
                  {
                    voice.used = false; // FIXME: envelope
                  }
                voice.ppos += step;
                if (voice.region->loop_mode == LoopMode::SUSTAIN || voice.region->loop_mode == LoopMode::CONTINUOUS)
                  {
                    if (voice.region->loop_end > voice.region->loop_start)
                      while (voice.ppos > voice.region->loop_end)
                        voice.ppos -= (voice.region->loop_end - voice.region->loop_start);
                  }
              }
          }
      }
    midi_events.clear();
    return 0;
  }
};


