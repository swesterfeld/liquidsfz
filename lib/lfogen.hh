// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#ifndef LIQUIDSFZ_LFOGEN_HH
#define LIQUIDSFZ_LFOGEN_HH

#include "utils.hh"

namespace LiquidSFZInternal
{

class LFOGen
{
public:
  enum OutputType {
    PITCH  = 0,
    VOLUME = 1,
    CUTOFF = 2
  };
  static constexpr uint MAX_OUTPUTS = 3;

private:
  Synth *synth_ = nullptr;
  int channel_ = 0;
  int sample_rate_ = 0;
  float smoothing_factor_ = 0;

  struct Wave;

  /* modulation links */
  struct ModLink
  {
    float *source;
    float  factor;
    float *dest;
  };
  struct LFO {
    const LFOParams *params = nullptr;
    Synth *synth = nullptr;
    float phase = 0;
    Wave *wave = nullptr;
    float next_freq_mod = 0;
    float freq_mod = 0;
    float freq = 0;
    float value = 0;
    float to_pitch = 0;
    float to_volume = 0;
    float to_cutoff = 0;
    uint  delay_len = 0;
    uint  fade_len = 0;
    uint  fade_pos = 0;

    /* sample and hold wave form */
    float sh_value = 0;
    int   last_sh_state = -1;
  };
  struct Output
  {
    bool    active     = false;
    float  *buffer    = nullptr;
    float   last_value = 0;
    float   value      = 0;
  };
  struct Wave
  {
    virtual float eval (LFO& lfo) = 0;
  };
  static Wave *get_wave (int wave);

  std::array<Output, MAX_OUTPUTS> outputs;
  bool first = false;
  std::vector<LFO> lfos;
  std::vector<ModLink> mod_links;

  void process_lfo (LFO& lfo, uint n_values);

  template<OutputType T>
  float
  post_function (float value);

  template<OutputType T>
  void
  write_output (uint start, uint n_values);
public:
  LFOGen (Synth *synth, const Limits& limits);

  void start (const Region& region, int channel, int sample_rate);
  void process (float *buffer, uint n_values);
  void update_ccs();

  const float *
  get (OutputType type) const
  {
    return outputs[type].buffer;
  }
  bool
  need_process() const
  {
    return lfos.size() != 0;
  }
  static bool supports_wave (int wave);
};

}

#endif /* LIQUIDSFZ_LFOGEN_HH */
