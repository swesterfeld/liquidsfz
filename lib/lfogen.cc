/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2021  Stefan Westerfeld
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

#include "voice.hh"
#include "synth.hh"

using namespace LiquidSFZInternal;

void
LFOGen::start (const Region& region, int channel, int sample_rate)
{
  channel_     = channel;
  sample_rate_ = sample_rate;

  lfos.resize (region.lfos.size()); // FIXME: not RT safe
  for (size_t i = 0; i < region.lfos.size(); i++)
    {
      lfos[i].params = &region.lfos[i];
      lfos[i].phase = 0;

      double delay = region.lfos[i].delay;
      delay += synth_->get_cc_vec_value (channel_, region.lfos[i].delay_cc);
      lfos[i].delay_len = std::max (delay * sample_rate, 0.0);

      double fade = region.lfos[i].fade;
      fade += synth_->get_cc_vec_value (channel_, region.lfos[i].fade_cc);
      lfos[i].fade_len = std::max (fade * sample_rate, 0.0);
      lfos[i].fade_pos = 0;

      first = true;
    }
}

void
LFOGen::process (float *lfo_speed_factor,
                 float *lfo_volume_factor,
                 float *lfo_cutoff_factor,
                 uint   n_frames)
{
  float target_speed = 1;
  float target_volume = 1;
  float target_cutoff = 1;

  for (auto& lfo : lfos)
    {
      lfo.to_pitch  = (synth_->get_cc_vec_value (channel_, lfo.params->pitch_cc)  + lfo.params->pitch) / 1200.;
      lfo.to_volume = (synth_->get_cc_vec_value (channel_, lfo.params->volume_cc) + lfo.params->volume);
      lfo.to_cutoff = (synth_->get_cc_vec_value (channel_, lfo.params->cutoff_cc) + lfo.params->cutoff) / 1200.;

      lfo.targets.clear(); // RT problem: should reserve()
      if (lfo.to_pitch)
        lfo.targets.push_back ({ &target_speed,  lfo.to_pitch });
      if (lfo.to_volume)
        lfo.targets.push_back ({ &target_volume, lfo.to_volume });
      if (lfo.to_cutoff)
        lfo.targets.push_back ({ &target_cutoff, lfo.to_cutoff });
    }

  for (uint i = 0; i < n_frames; i++)
    {
      if ((i & 31) == 0)
        {
          target_speed = 0;
          target_volume = 0;
          target_cutoff = 0;

          for (auto& lfo : lfos)
            {
              if (lfo.delay_len)
                continue;

              float value = sin (lfo.phase);

              if (lfo.fade_pos < lfo.fade_len)
                value *= float (lfo.fade_pos) / lfo.fade_len;

              for (auto& t : lfo.targets)
                *t.target += value * t.multiply;
            }
          target_speed = (target_speed != 0) ? exp2f (target_speed) : 1;
          target_volume = (target_volume != 0) ? db_to_factor (target_volume) : 1;
          target_cutoff = (target_cutoff != 0) ? exp2f (target_cutoff) : 1;
        }
      if (first)
        {
          last_speed_factor = target_speed;
          last_volume_factor = target_volume;
          last_cutoff_factor = target_cutoff;
          first = false;
        }
      last_speed_factor  = target_speed * 0.01f + 0.99f * last_speed_factor;
      lfo_speed_factor[i] = last_speed_factor;

      last_volume_factor = target_volume * 0.01f + 0.99f * last_volume_factor;
      lfo_volume_factor[i] = last_volume_factor;

      last_cutoff_factor = target_cutoff * 0.01f + 0.99f * last_cutoff_factor;
      lfo_cutoff_factor[i] = last_cutoff_factor;

      for (auto& lfo : lfos)
        {
          double freq = synth_->get_cc_vec_value (channel_, lfo.params->freq_cc) + lfo.params->freq;
          lfo.phase += (freq * 2 * M_PI) / sample_rate_;

          if (lfo.delay_len)
            lfo.delay_len--;
          if (lfo.fade_pos < lfo.fade_len)
            lfo.fade_pos++;
        }
    }
}
