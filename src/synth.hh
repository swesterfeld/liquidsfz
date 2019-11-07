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

#ifndef LIQUIDSFZ_SYNTH_HH
#define LIQUIDSFZ_SYNTH_HH

#include <random>
#include <algorithm>
#include <assert.h>

#include "loader.hh"
#include "utils.hh"
#include "envelope.hh"
#include "voice.hh"

namespace LiquidSFZInternal
{

class Synth
{
  std::minstd_rand random_gen;
  uint sample_rate_ = 44100; // default
  Loader sfz_loader;
  uint64_t global_frame_count = 0;
  std::vector<Voice> voices_;

public:
  Synth()
  {
    set_max_voices (64); // default
  }
  void
  set_sample_rate (uint sample_rate)
  {
    sample_rate_ = sample_rate;
  }
  void
  set_max_voices (uint n_voices)
  {
    voices_.resize (n_voices);
  }
  bool
  load (const std::string& filename)
  {
    return sfz_loader.parse (filename);
  }
  Voice *
  alloc_voice()
  {
    for (auto& v : voices_)
      {
        if (!v.used_)
          {
            return &v;
          }
      }
    log_debug ("alloc_voice: no voices left\n");
    return nullptr;
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
                        auto voice = alloc_voice();
                        if (voice)
                          voice->start (region, chan, key, vel, 0.0 /* time_since_note_on */, global_frame_count, sample_rate_);
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
    for (auto& voice : voices_)
      {
        if (voice.used_ && voice.trigger_ == Trigger::ATTACK  && voice.channel_ == chan && voice.key_ == key && voice.region_->loop_mode != LoopMode::ONE_SHOT)
          {
            voice.envelope_.release();

            int vel = voice.velocity_;
            for (auto& region : sfz_loader.regions)
              {
                if (region.lokey <= key && region.hikey >= key &&
                    region.lovel <= vel && region.hivel >= vel &&
                    region.trigger == Trigger::RELEASE)
                  {
                    double time_since_note_on = (global_frame_count - voice.start_frame_count_) / double (sample_rate_);

                    auto voice = alloc_voice();
                    if (voice)
                      voice->start (region, chan, key, vel, time_since_note_on, global_frame_count, sample_rate_);
                  }
              }
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
  add_midi_event (uint offset, const unsigned char *midi_data)
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
    for (auto& voice : voices_)
      {
        if (voice.used_)
          voice.process (outputs, nframes);
      }
    midi_events.clear();
    global_frame_count += nframes;
    return 0;
  }
};

}

#endif /* LIQUIDSFZ_SYNTH_HH */
