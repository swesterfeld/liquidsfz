// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#ifndef LIQUIDSFZ_ENVELOPE_HH
#define LIQUIDSFZ_ENVELOPE_HH

#include "loader.hh"

#include <algorithm>

#include <assert.h>

namespace LiquidSFZInternal
{

class Envelope
{
public:
  enum class Shape { EXPONENTIAL, LINEAR };
private:
  /* values in seconds */
  float delay_ = 0;
  float attack_ = 0;
  float hold_ = 0;
  float decay_ = 0;
  float sustain_ = 0; /* <- percent */
  float release_ = 0;

  int delay_len_ = 0;
  int attack_len_ = 0;
  int hold_len_ = 0;
  int decay_len_ = 0;
  int release_len_ = 0;
  int stop_len_ = 0;
  int off_time_len_ = 0;
  float sustain_level_ = 0;

  enum class State { START, DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE, DONE };

  State state_ = State::DONE;
  Shape shape_ = Shape::EXPONENTIAL;

  struct SlopeParams {
    int len       = 0;

    double factor = 0;
    double delta  = 0;
    double end    = 0;
  } params_;

  double level_ = 0;

public:
  void
  set_shape (Shape shape)
  {
    shape_ = shape;
  }
  void
  set_delay (float f)
  {
    delay_ = f;
  }
  void
  set_attack (float f)
  {
    attack_ = f;
  }
  void
  set_hold (float f)
  {
    hold_ = f;
  }
  void
  set_decay (float f)
  {
    decay_ = f;
  }
  void
  set_sustain (float f)
  {
    sustain_ = f;
  }
  void
  set_release (float f)
  {
    release_ = f;
  }
  void
  start (const Region& r, int sample_rate)
  {
    delay_len_ = std::max (int (sample_rate * delay_), 0);
    attack_len_ = std::max (int (sample_rate * attack_), 0);
    hold_len_ = std::max (int (sample_rate * hold_), 0);
    decay_len_ = std::max (int (sample_rate * decay_), 0);
    sustain_level_ = std::clamp<float> (sustain_ * 0.01, 0, 1); // percent->level
    release_len_ = std::max (int (sample_rate * release_), 1);
    stop_len_ = std::max (int (sample_rate * 0.030), 1);
    off_time_len_ = std::max (int (sample_rate * r.off_time), 1);

    level_ = 0;
    state_ = State::START;
    next_state();
  }

  void
  next_state()
  {
    /* this funnction allows skipping stages - for instance if the delay and
     * hold time is zero, we would only have the stages A D S R
     */
    if (state_ == State::START)
      {
        state_ = State::DELAY;
        if (delay_len_)
          {
            compute_slope_params (delay_len_, 0, 0, State::DELAY);
            return;
          }
        level_ = 0;
      }
    if (state_ == State::DELAY)
      {
        state_ = State::ATTACK;
        if (attack_len_)
          {
            compute_slope_params (attack_len_, 0, 1, State::ATTACK);
            return;
          }
        level_ = 1;
      }
    if (state_ == State::ATTACK)
      {
        state_ = State::HOLD;
        if (hold_len_)
          {
            compute_slope_params (hold_len_, 1, 1, State::HOLD);
            return;
          }
       level_ = 1;
     }
   if (state_ == State::HOLD)
      {
        state_ = State::DECAY;
        if (decay_len_)
          {
            compute_slope_params (decay_len_, 1, sustain_level_, State::DECAY);
            return;
          }
        level_ = sustain_level_;
      }
   if (state_ == State::DECAY)
      {
        state_ = State::SUSTAIN;
      }
  }
  void
  stop (OffMode off_mode)
  {
    int len = 0;
    switch (off_mode)
      {
        case OffMode::NORMAL: len = release_len_;
                              break;
        case OffMode::TIME:   len = off_time_len_;
                              break;
        case OffMode::FAST:   len = stop_len_;
                              break;

      }
    state_ = State::RELEASE;
    compute_slope_params (len, level_, 0, State::RELEASE);
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

    if (param_state == State::ATTACK || param_state == State::DELAY || param_state == State::HOLD || shape_ == Shape::LINEAR)
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

  bool
  is_constant()
  {
    return state_ == State::SUSTAIN || state_ == State::DONE;
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
        if (state_ == State::RELEASE)
          {
            state_ = State::DONE;
          }
        else
          {
            next_state();
          }
      }
    return level_;
  }
};

}

#endif /* LIQUIDSFZ_ENVELOPE_HH */
