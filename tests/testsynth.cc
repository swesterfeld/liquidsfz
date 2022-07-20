// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <cmath>
#include <cstdio>
#include <cassert>
#include <unistd.h>

#include <sndfile.h>

#include <vector>

#include "liquidsfz.hh"

using std::vector;
using std::max;
using LiquidSFZ::Synth;

void
write_sample (const vector<float>& samples, int rate)
{
  SF_INFO sfinfo = {0,};
  sfinfo.samplerate = rate;
  sfinfo.channels = 1;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  SNDFILE *sndfile = sf_open ("testsynth.wav", SFM_WRITE, &sfinfo);
  assert (sndfile);

  sf_count_t count = sf_writef_float (sndfile, &samples[0], samples.size());
  assert (count == sf_count_t (samples.size()));
  sf_close (sndfile);
}

void
write_sfz (const std::string& sfz)
{
  FILE *sfz_file = fopen ("testsynth.sfz", "w");
  assert (sfz_file);

  fprintf (sfz_file, "%s\n", sfz.c_str());
  fclose (sfz_file);
}

float
freq_from_zero_crossings (const vector<float>& samples, int sample_rate)
{
  int zero_crossings = 0;
  bool last_gt0 = false;
  for (size_t i = 0; i < samples.size(); i++)
    {
      bool gt0 = samples[i] > 0;
      if (gt0 != last_gt0)
        {
          zero_crossings++;
          last_gt0 = gt0;
        }
    }
  return zero_crossings * 0.5 * sample_rate / samples.size();
}

float
peak (const vector<float>& samples)
{
  float peak = 0;
  for (auto s : samples)
    peak = max (fabsf (s), peak);
  return peak;
}

int
main()
{
  int sample_rate = 44100;
  vector<float> samples;
  for (int i = 0; i < 441; i++)
    samples.push_back (sin (i * 2 * M_PI * 100 / sample_rate));

  write_sample (samples, sample_rate);
  write_sfz ("<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440 pan_cc10=0 /* disable CC10 */");

  Synth synth;
  synth.set_sample_rate (sample_rate);
  synth.set_live_mode (false);
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  synth.add_event_note_on (0, 0, 60, 127);

  vector<float> out_left (sample_rate), out_right (sample_rate);
  float *outputs[2] = { out_left.data(), out_right.data() };
  synth.process (outputs, sample_rate);

  double freq, volume, expect_volume, vdiff_percent;

  freq = freq_from_zero_crossings (out_left, sample_rate);
  assert (freq > 99 && freq < 101);

  freq = freq_from_zero_crossings (out_right, sample_rate);
  assert (freq > 99 && freq < 101);

  expect_volume = pow (100. / 127, 2) / sqrt (2);

  volume = peak (out_left);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 0.001);

  volume = peak (out_right);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 0.001);

  synth.all_sound_off();

  synth.add_event_note_on (0, 0, 48, 127);
  synth.process (outputs, sample_rate);

  freq = freq_from_zero_crossings (out_left, sample_rate);
  assert (freq > 49 && freq < 51);

  freq = freq_from_zero_crossings (out_right, sample_rate);
  assert (freq > 49 && freq < 51);

  write_sfz ("<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440 volume_cc7=0 /* disable CC7 */");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  synth.add_event_note_on (0, 0, 60, 127);
  synth.process (outputs, sample_rate);

  /* center panning - this is not exact as we have CC7=64 of 127 */
  expect_volume = 1 / sqrt (2);
  volume = peak (out_left);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 1);

  volume = peak (out_right);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 1);

  /* left panning */
  synth.add_event_cc (0, 0, 10, 0);
  synth.process (outputs, sample_rate); // this takes a bit until it affects our note
  synth.process (outputs, sample_rate);

  vdiff_percent = 100 * fabs (peak (out_left) - 1);
  assert (vdiff_percent < 0.001);

  vdiff_percent = 100 * fabs (peak (out_right));
  assert (vdiff_percent < 0.001);

  /* right panning */
  synth.add_event_cc (0, 0, 10, 127);
  synth.process (outputs, sample_rate); // this takes a bit until it affects our note
  synth.process (outputs, sample_rate);

  vdiff_percent = 100 * fabs (peak (out_left));
  assert (vdiff_percent < 0.001);

  vdiff_percent = 100 * fabs (peak (out_right) - 1);
  assert (vdiff_percent < 0.001);

  unlink ("testsynth.sfz");
  unlink ("testsynth.wav");
}
