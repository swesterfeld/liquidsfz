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
      lfos[i].synth  = synth_;

      double phase = region.lfos[i].phase;
      phase += synth_->get_cc_vec_value (channel_, region.lfos[i].phase_cc);
      lfos[i].phase = std::clamp (phase, 0.0, 1.0);

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

inline void
LFOGen::process_lfo (LFO& lfo, uint n_values)
{
  if (!lfo.delay_len)
    {
      float value = lfo.wave->eval (lfo);

      if (lfo.fade_pos < lfo.fade_len)
        value *= float (lfo.fade_pos) / lfo.fade_len;

      for (auto& t : lfo.targets)
        *t.target += value * t.multiply;
    }

  if (lfo.delay_len)
    {
      if (lfo.delay_len >= n_values)
        {
          lfo.delay_len -= n_values;
          n_values = 0;
        }
      else
        {
          n_values -= lfo.delay_len;
          lfo.delay_len = 0;
        }
    }
  if (lfo.fade_pos < lfo.fade_len)
    {
      lfo.fade_pos = std::min (lfo.fade_len, lfo.fade_pos + n_values);
    }

  lfo.phase += n_values * (lfo.freq + lfo.freq_mod) / sample_rate_;
  while (lfo.phase > 1)
    lfo.phase -= 1;
}

LFOGen::Wave *
LFOGen::get_wave (int wave)
{
  static struct WaveTri : public Wave
  {
    float
    eval (LFO& lfo) override
    {
      if (lfo.phase < 0.25f) return lfo.phase * 4;
      if (lfo.phase < 0.75f) return 2 - lfo.phase * 4;
      return -4 + lfo.phase * 4;
    }
  } wave_tri;
  static struct WaveSin : public Wave
  {
    float eval (LFO& lfo) override { return sinf (lfo.phase * 2 * M_PI); };
  } wave_sin;
  static struct WavePulse75 : public Wave
  {
    float eval (LFO& lfo) override { return lfo.phase < 0.75 ? 1 : -1; };
  } wave_pulse75;
  static struct WaveSquare : public Wave
  {
    float eval (LFO& lfo) override { return lfo.phase < 0.5 ? 1 : -1; };
  } wave_square;
  static struct WavePulse25 : public Wave
  {
    float eval (LFO& lfo) override { return lfo.phase < 0.25 ? 1 : -1; };
  } wave_pulse25;
  static struct WavePulse125 : public Wave
  {
    float eval (LFO& lfo) override { return lfo.phase < 0.125 ? 1 : -1; };
  } wave_pulse125;
  static struct WaveSawUp : public Wave
  {
    float eval (LFO& lfo) override { return lfo.phase * 2 - 1; };
  } wave_saw_up;
  static struct WaveSawDown : public Wave
  {
    float eval (LFO& lfo) override { return 1 - lfo.phase * 2; };
  } wave_saw_down;
  static struct WaveSH : public Wave
  {
    float
    eval (LFO& lfo) override
    {
      int sh_state = lfo.phase < 0.5;
      if (lfo.last_sh_state != sh_state)
        {
          lfo.sh_value = lfo.synth->normalized_random_value() * 2 - 1;
          lfo.last_sh_state = sh_state;
        }
      return lfo.sh_value;
    };
  } wave_sh;

  switch (wave)
  {
    case 0: return &wave_tri;
    case 1: return &wave_sin;
    case 2: return &wave_pulse75;
    case 3: return &wave_square;
    case 4: return &wave_pulse25;
    case 5: return &wave_pulse125;
    case 6: return &wave_saw_up;
    case 7: return &wave_saw_down;
    case 12: return &wave_sh;
    default: return nullptr;
  }
}

bool
LFOGen::supports_wave (int wave)
{
  return get_wave (wave) != nullptr;
}

void
LFOGen::process (float *lfo_buffer, uint n_values)
{
  if (!lfos.size())
    return;

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

      for (auto lm : lfo.params->lfo_mod)
        {
          float to_lfo_freq = (synth_->get_cc_vec_value (channel_, lm.lfo_freq_cc) + lm.lfo_freq);
          if (to_lfo_freq)
            lfo.targets.push_back ({ &lfos[lm.to_index].next_freq_mod, to_lfo_freq });
        }
      lfo.wave = get_wave (lfo.params->wave);
    }

  for (auto& output : outputs)
    {
      if (output.active)
        {
          output.buffer = lfo_buffer;
          lfo_buffer += n_values;
        }
    }

  uint i = 0;
  while (i < n_values)
    {
      constexpr uint block_size = 32;
      uint todo = std::min (block_size, n_values - i);

      for (auto& output : outputs)
        output.value = 0;

      for (auto& lfo : lfos)
        {
          lfo.freq_mod = lfo.next_freq_mod;
          lfo.next_freq_mod = 0;
        }
      for (auto& lfo : lfos)
        process_lfo (lfo, todo);

      outputs[PITCH].value  = (outputs[PITCH].value != 0) ? exp2f (outputs[PITCH].value) : 1;
      outputs[VOLUME].value = (outputs[VOLUME].value != 0) ? db_to_factor (outputs[VOLUME].value) : 1;
      outputs[CUTOFF].value = (outputs[CUTOFF].value != 0) ? exp2f (outputs[CUTOFF].value) : 1;

      if (first)
        {
          for (auto& output : outputs)
            output.last_value = output.value;

          first = false;
        }

      for (uint o = 0; o < outputs.size(); o++)
        smooth (OutputType (o), i, todo);

      i += todo;
    }
}
