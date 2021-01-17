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

  for (auto& output : outputs) // reset outputs
    output = Output();

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

      if (lfos[i].params->pitch || !lfos[i].params->pitch_cc.empty())
        outputs[PITCH].active = true;

      if (lfos[i].params->volume || !lfos[i].params->volume_cc.empty())
        outputs[VOLUME].active = true;

      if (lfos[i].params->cutoff || !lfos[i].params->cutoff_cc.empty())
        outputs[CUTOFF].active = true;

      first = true;
    }
}

void
LFOGen::process (float *lfo_speed_factor,
                 float *lfo_volume_factor,
                 float *lfo_cutoff_factor,
                 uint   n_frames)
{
  for (auto& lfo : lfos)
    {
      lfo.to_pitch  = (synth_->get_cc_vec_value (channel_, lfo.params->pitch_cc)  + lfo.params->pitch) / 1200.;
      lfo.to_volume = (synth_->get_cc_vec_value (channel_, lfo.params->volume_cc) + lfo.params->volume);
      lfo.to_cutoff = (synth_->get_cc_vec_value (channel_, lfo.params->cutoff_cc) + lfo.params->cutoff) / 1200.;
      lfo.freq      = (synth_->get_cc_vec_value (channel_, lfo.params->freq_cc)   + lfo.params->freq);

      lfo.targets.clear(); // RT problem: should reserve()
      if (lfo.to_pitch)
        lfo.targets.push_back ({ &outputs[PITCH].value,  lfo.to_pitch });
      if (lfo.to_volume)
        lfo.targets.push_back ({ &outputs[VOLUME].value, lfo.to_volume });
      if (lfo.to_cutoff)
        lfo.targets.push_back ({ &outputs[CUTOFF].value, lfo.to_cutoff });
    }

  if (outputs[PITCH].active)
    outputs[PITCH].buffer = lfo_speed_factor;

  if (outputs[VOLUME].active)
    outputs[VOLUME].buffer = lfo_volume_factor;

  if (outputs[CUTOFF].active)
    outputs[CUTOFF].buffer = lfo_cutoff_factor;

  uint i = 0;
  while (i < n_frames)
    {
      for (auto& output : outputs)
        output.value = 0;

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
      outputs[PITCH].value  = (outputs[PITCH].value != 0) ? exp2f (outputs[PITCH].value) : 1;
      outputs[VOLUME].value = (outputs[VOLUME].value != 0) ? db_to_factor (outputs[VOLUME].value) : 1;
      outputs[CUTOFF].value = (outputs[CUTOFF].value != 0) ? exp2f (outputs[CUTOFF].value) : 1;

      if (first)
        {
          for (auto& output : outputs)
            output.last_value = output.value;

          first = false;
        }

      constexpr uint block_size = 32;
      uint todo = std::min (block_size, n_frames - i);

      for (uint o = 0; o < outputs.size(); o++)
        smooth (OutputType (o), i, todo);

      for (auto& lfo : lfos)
        {
          // FIXME: phase increment broken (todo)
          lfo.phase += todo * (lfo.freq * 2 * M_PI) / sample_rate_;

          if (lfo.delay_len)
            {
              if (lfo.delay_len >= todo)
                {
                  lfo.delay_len -= todo;
                  todo = 0;
                }
              else
                {
                  todo -= lfo.delay_len;
                  lfo.delay_len = 0;
                }
            }
          if (lfo.fade_pos < lfo.fade_len)
            {
              lfo.fade_pos = std::min (lfo.fade_len, lfo.fade_pos + todo);
            }
        }
      i += todo;
    }
}
