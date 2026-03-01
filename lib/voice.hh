// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include "envelope.hh"
#include "filter.hh"
#include "lfogen.hh"

namespace LiquidSFZInternal
{

class SampleReader
{
  Sample::PlayHandle *play_handle_ = nullptr;
  const Sample *cached_sample_ = nullptr;
  int relative_pos_ = 0;
  int end_pos_ = 0;
  int channels_ = 0;
  int loop_start_ = -1;
  int loop_end_ = -1;
  int upsample_buffer_size_ = 0;
  static constexpr int MAX_UPSAMPLE_BUFFER_SIZE = 10;
  std::array<float, MAX_UPSAMPLE_BUFFER_SIZE * 4> samples_; // max: 2x upsampling, stereo
  int last_index_ = -1000;
public:
  void
  restart (Sample::PlayHandle *play_handle, const Sample *cached_sample, int upsample)
  {
    channels_ = cached_sample->channels();
    relative_pos_ = 0;
    end_pos_ = (cached_sample->n_samples() / channels_ + 32) * upsample;
    play_handle_ = play_handle;
    cached_sample_ = cached_sample;
    loop_start_ = loop_end_ = -1;
    last_index_ = -1000;
    upsample_buffer_size_ = 0;
    samples_.fill (0);
  }
  void
  set_loop (int loop_start, int loop_end)
  {
    loop_start_ = loop_start;
    loop_end_ = loop_end;
  }
  void
  stop_loop()
  {
    loop_start_ = -1;
    loop_end_ = -1;
  }

  template<int UPSAMPLE, int CHANNELS, int INTERP_POINTS>
  const float *skip (int pos);

  bool
  done()
  {
    return relative_pos_ > end_pos_;
  }
};

class Voice
{
  LinearSmooth left_gain_;
  LinearSmooth right_gain_;
  LinearSmooth width_factor_;

  struct FImpl {
    Filter              filter;
    LinearSmooth        cutoff_smooth;
    LinearSmooth        resonance_smooth;
    const FilterParams *params;
  } fimpl_, fimpl2_;

  Sample::PlayHandle play_handle_;

  Envelope filter_envelope_;
  float    filter_envelope_depth_ = 0;

  LFOGen lfo_gen_;

  float volume_gain_ = 0;
  float amplitude_gain_ = 0;
  float velocity_gain_ = 0;
  float rt_decay_gain_ = 0;
  float pan_left_gain_ = 0;
  float pan_right_gain_ = 0;
  float cc7_cc10_left_gain_ = 0;
  float cc7_cc10_right_gain_ = 0;

  float amp_random_gain_ = 0;
  float pitch_random_cent_ = 0;
  uint  delay_samples_ = 0;

  void update_volume_gain();
  void update_amplitude_gain();
  void update_pan_gain();
  void update_cc7_cc10_gain();
  void update_lr_gain (bool now);
  void update_width_factor (bool now);

  float amp_value (float vnorm, const EGParam& amp_param);

  void  start_filter (FImpl& fi, const FilterParams *params);
  void  update_cutoff (FImpl& fi, bool now);
  void  update_resonance (FImpl& fi, bool now);

  LinearSmooth replay_speed_;
  float        pitch_bend_value_ = 0; // [-1:1]

  SampleReader sample_reader_;
  int          quality_ = 0;

  void set_pitch_bend (int value);
  void update_replay_speed (bool now);
public:
  Synth *synth_;
  int sample_rate_ = 44100;
  int channel_ = 0;
  int key_ = 0;
  int velocity_ = 0;
  bool loop_enabled_ = false;

  enum State {
    ACTIVE,
    SUSTAIN,
    RELEASED,
    IDLE
  };
  State state_ = IDLE;

  double ppos_ = 0;
  int64_t last_ippos_ = 0;
  uint64_t start_frame_count_ = 0;
  Trigger trigger_ = Trigger::ATTACK;
  Envelope envelope_;

  const Region *region_ = nullptr;

  Voice (Synth *synth,
         const Limits& limits) :
    lfo_gen_ (synth, limits),
    synth_ (synth)
  {
  }
  double pan_stereo_factor (double region_pan, int ch);
  double velocity_track_factor (const Region& r, int midi_velocity);

  void start (const Region& region, int channel, int key, int velocity, double time_since_note_on, uint64_t global_frame_count, uint sample_rate);
  void stop (OffMode off_mode);
  void kill();
  void process (float **outputs, uint n_frames);
  template<int QUALITY, int CHANNELS>
  void process_impl (float **outputs, uint n_frames);
  void process_filter (FImpl& fi, bool envelope, float *left, float *right, uint n_frames, const float *lfo_cutoff_factor);
  void process_width (float *out_l, float *out_r, uint n_frames);
  uint off_by();
  void update_cc (int controller);
  void update_gain();

  void update_pitch_bend (int bend);

  float xfin_gain (int value, int lo, int hi, XFCurve curve);
  float xfout_gain (int value, int lo, int hi, XFCurve curve);
  float apply_xfcurve (float f, XFCurve curve);
};

}
