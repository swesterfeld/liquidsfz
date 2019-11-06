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

class Envelope
{
  int attack_len_ = 0;
  int decay_len_ = 0;
  int release_len_ = 0;
  float sustain_level_ = 0;

  enum class State { ATTACK, DECAY, SUSTAIN, RELEASE, DONE };

  State state_ = State::DONE;

  struct SlopeParams {
    int len;

    double factor;
    double delta;
    double end;
  } params_;

  double level_ = 0;

public:
  void
  start (const Region& r, int sample_rate)
  {
    attack_len_ = std::max (int (sample_rate * r.ampeg_attack), 1);
    decay_len_ = std::max (int (sample_rate * r.ampeg_decay), 1);
    sustain_level_ = std::clamp<float> (r.ampeg_sustain * 0.01, 0, 1); // percent->level
    release_len_ = std::max (int (sample_rate * r.ampeg_release), 1);

    level_ = 0;
    state_ = State::ATTACK;

    compute_slope_params (attack_len_, 0, 1, State::ATTACK);
  }
  void
  release()
  {
    state_ = State::RELEASE;

    compute_slope_params (release_len_, level_, 0, State::RELEASE);
  }
  bool
  done()
  {
    return state_ == State::DONE;
  }
  void
  compute_slope_params (int len, float start_x, float end_x, State param_state)
  {
    params_.end = end_x;

    if (param_state == State::ATTACK)
      {
        // linear
        params_.len    = len;
        params_.delta  = (end_x - start_x) / params_.len;
        params_.factor = 1;
      }
    else
      {
        assert (param_state == State::DECAY || param_state == State::RELEASE);

        // exponential

        /* true exponential decay doesn't ever reach zero; therefore we need to
         * fade out early
         */
        const double RATIO = 0.001; // -60dB or 0.1% of the original height;

        /* compute iterative exponential decay parameters from inputs:
         *
         *   - len:           half life time
         *   - RATIO:         target ratio (when should we reach zero)
         *   - start_x/end_x: level at start/end of the decay slope
         *
         * iterative computation of next value (should be done params.len times):
         *
         *    value = value * params.factor + params.delta
         */
#if 0
        const double f = -log ((RATIO + 1)/(RATIO + 0.5))/len;
        params.len    = -log ((RATIO + 1) / RATIO)/f;
#endif
        const double f = -log ((RATIO + 1) / RATIO) / len;
        params_.len    = len;
        params_.factor = exp (f);
        params_.delta  = (end_x - RATIO * (start_x - end_x)) * (1 - params_.factor);
      }
  }

  float
  get_next()
  {
    if (state_ == State::SUSTAIN || state_ == State::DONE)
      return level_;

    level_ = level_ * params_.factor + params_.delta;
    params_.len--;
    if (!params_.len)
      {
        level_ = params_.end;

        if (state_ == State::ATTACK)
          {
            compute_slope_params (decay_len_, 1, sustain_level_, State::DECAY);
            state_ = State::DECAY;
          }
        else if (state_ == State::DECAY)
          {
            state_ = State::SUSTAIN;
          }
        else if (state_ == State::RELEASE)
          {
            state_ = State::DONE;
          }
      }
    return level_;
  }
};
