// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <cmath>
#include <cstdio>
#include <cassert>
#include <unistd.h>

#include <sndfile.h>
#include <vector>

#include "liquidsfz.hh"
#include "config.h"

#if HAVE_FFTW
#include <fftw3.h>
#endif

using std::vector;
using std::max;
using LiquidSFZ::Synth;

struct SineDetectPartial
{
  double freq;
  double mag;
};

class QInterpolator
{
  double a, b, c;

public:
  QInterpolator (double y1, double y2, double y3)
  {
    a = (y1 + y3 - 2*y2) / 2;
    b = (y3 - y1) / 2;
    c = y2;
  }
  double
  eval (double x)
  {
    return a * x * x + b * x + c;
  }
  double
  x_max()
  {
    return -b / (2 * a);
  }
};

inline double
window_cos (double x) /* von Hann window */
{
  if (fabs (x) > 1)
    return 0;
  return 0.5 * cos (x * M_PI) + 0.5;
}

void
fft (const uint n_values, float *r_values_in, float *ri_values_out)
{
#if HAVE_FFTW
  auto plan_fft = fftwf_plan_dft_r2c_1d (n_values, r_values_in, (fftwf_complex *) ri_values_out, FFTW_ESTIMATE | FFTW_PRESERVE_INPUT);

  fftwf_execute_dft_r2c (plan_fft, r_values_in, (fftwf_complex *) ri_values_out);

  // usually we should keep the plan, but for this simple test program, the fft
  // is only computed once, so we can destroy the plan here
  fftwf_destroy_plan (plan_fft);
#endif
}

double
db (double x)
{
  return 20 * log10 (std::max (x, 0.00000001));
}

double
db_to_factor (double db)
{
  double factor = db / 20; /* Bell */
  return pow (10, factor);
}

vector<SineDetectPartial>
sine_detect (double mix_freq, const vector<float>& signal)
{
  /* possible improvements for this code
   *
   *  - could produce phases (odd-centric window)
   *  - could eliminate the sines to produce residual signal (spectral subtract)
   */
  vector<SineDetectPartial> partials;

  constexpr double MIN_PADDING = 4;

  size_t padded_length = 2;
  while (signal.size() * MIN_PADDING >= padded_length)
    padded_length *= 2;

  vector<float> padded_signal;
  float window_weight = 0;
  for (size_t i = 0; i < signal.size(); i++)
    {
      const float w = window_cos ((i - signal.size() * 0.5) / (signal.size() * 0.5));
      window_weight += w;
      padded_signal.push_back (signal[i] * w);
    }
  padded_signal.resize (padded_length);

  vector<float> fft_values (padded_signal.size() + 2);
  fft (padded_signal.size(), padded_signal.data(), fft_values.data());

  vector<float> mag_values;
  for (size_t i = 0; i < fft_values.size(); i += 2)
    mag_values.push_back (sqrt (fft_values[i] * fft_values[i] + fft_values[i + 1] * fft_values[i + 1]));

  for (size_t x = 1; x + 2 < mag_values.size(); x++)
    {
      /* check for peaks
       *  - single peak : magnitude of the middle value is larger than
       *                  the magnitude of the left and right neighbour
       *  - double peak : two values in the spectrum have equal magnitude,
         *                this must be larger than left and right neighbour
       */
      const auto [m1, m2, m3, m4] = std::tie (mag_values[x - 1], mag_values[x], mag_values[x + 1],  mag_values[x + 2]);
      if ((m1 < m2 && m2 > m3) || (m1 < m2 && m2 == m3 && m3 > m4))
        {
          size_t xs, xe;
          for (xs = x - 1; xs > 0 && mag_values[xs] < mag_values[xs + 1]; xs--);
          for (xe = x + 1; xe < (mag_values.size() - 1) && mag_values[xe] > mag_values[xe + 1]; xe++);

          const double normalized_peak_width = double (xe - xs) * signal.size() / padded_length;

          const double mag1 = db (mag_values[x - 1]);
          const double mag2 = db (mag_values[x]);
          const double mag3 = db (mag_values[x + 1]);
          QInterpolator mag_interp (mag1, mag2, mag3);
          double x_max = mag_interp.x_max();
          double peak_mag_db = mag_interp.eval (x_max);
          double peak_mag = db_to_factor (peak_mag_db) * (2 / window_weight);

          if (peak_mag > 0.0001 && normalized_peak_width > 2.9)
            {
              SineDetectPartial partial;
              partial.freq = (x + x_max) * mix_freq / padded_length;
              partial.mag  = peak_mag;
              partials.push_back (partial);
              // printf ("%f %f %f\n", (x + x_max) * mix_freq / padded_length, peak_mag * (2 / window_weight), normalized_peak_width);
            }
        }
    }

  return partials;
}

void
write_sample (const vector<float>& samples, int rate, int channels = 1)
{
  SF_INFO sfinfo = {0,};
  sfinfo.samplerate = rate;
  sfinfo.channels = channels;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  SNDFILE *sndfile = sf_open ("testsynth.wav", SFM_WRITE, &sfinfo);
  assert (sndfile);

  sf_count_t count = sf_writef_float (sndfile, &samples[0], samples.size() / channels);
  assert (count == sf_count_t (samples.size() / channels));
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
max_location (const vector<float>& samples)
{
  int loc = -1;
  float mx = -1;
  for (size_t l = 0; l < samples.size(); l++)
    {
      if (samples[l] > mx)
        {
          loc = l;
          mx = samples[l];
        }
    }
  return loc;
}

vector<float>
cut_ms (const vector<float>& samples, int start_ms, int end_ms, int sample_rate)
{
  vector<float> result;
  for (size_t i = 0; i < samples.size(); i++)
    {
      double ms = i * 1000. / sample_rate;
      if (ms >= start_ms && ms <= end_ms)
        result.push_back (samples[i]);
    }
  return result;
}

void
test_tiny_loop()
{
#if HAVE_FFTW
  for (int channels = 1; channels <= 2; channels++)
    {
      int sample_rate = 44100;
      vector<float> samples (100 * channels);
      std::fill (samples.begin(), samples.end(), 1);
      for (int i = 0; i < 10; i++)
        {
          for (int c = 0; c < channels; c++)
            {
              double v;
              if (c == 0)
                v = sin (i * 2 * M_PI / 10);
              else
                v = sin (0.3 + i * 2 * M_PI / 10) * 0.5;

              samples[(50 + i) * channels + c] = v;
            }
        }

      write_sample (samples, sample_rate, channels);
      write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 loop_mode=loop_continuous loop_start=50 loop_end=59");

      Synth synth;
      synth.set_sample_rate (sample_rate);
      synth.set_live_mode (false);
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      for (int c = 0; c < channels; c++)
        {
          printf ("test tiny loop %s (channel %d/%d)\n", channels == 1 ? "mono" : "stereo", c + 1, channels);
          for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
            {
              synth.all_sound_off();
              synth.set_sample_quality (sample_quality);
              synth.set_gain (c == 0 ? sqrt(2) : 2 * sqrt (2));
              synth.add_event_note_on (0, 0, 24, 127); // 3 octaves down

              vector<float> out_left (sample_rate), out_right (sample_rate);
              float *outputs[2] = { out_left.data(), out_right.data() };
              synth.process (outputs, sample_rate);

              auto partials = sine_detect (sample_rate, c == 0 ? out_left : out_right);
              std::sort (partials.begin(), partials.end(), [] (auto a, auto b) { return a.mag > b.mag; });
              assert (partials.size() >= 2);

              double amag_max;
              double f_expect = 4410. / 8;
              if (sample_quality == 1)
                amag_max = -38;
              if (sample_quality == 2)
                amag_max = -69;
              if (sample_quality == 3)
                amag_max = -77;

              printf ("  - quality=%d freq=%f (expect %f) mag=%f | alias_freq=%f amag=%f (max %f)\n", sample_quality,
                  partials[0].freq, f_expect, db (partials[0].mag),
                  partials[1].freq, db (partials[1].mag),
                  amag_max);
              assert (db (partials[0].mag) >= -0.3 && db (partials[0].mag) < 0);
              assert (fabs (partials[0].freq - f_expect) < 0.01);
            }
        }
    }
#endif
}

void
test_interp_time_align()
{
  int sample_rate = 44100;
  vector<float> samples (100);
  samples[50] = 1;
  write_sample (samples, sample_rate);
  write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0");

  Synth synth;
  synth.set_sample_rate (sample_rate * 8);
  synth.set_live_mode (false);
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  printf ("test interpolation time align\n");
  for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
    {
      synth.all_sound_off();
      synth.set_sample_quality (sample_quality);
      synth.set_gain (sqrt(2));
      synth.add_event_note_on (0, 0, 60, 127);

      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };
      synth.process (outputs, sample_rate);
      assert (max_location (out_left) == 50 * 8);
      assert (max_location (out_right) == 50 * 8);
      assert (peak (out_left) > 0.8 && peak (out_left) < 1.1);
      printf (" - sample_quality = %d peak = %f %d\n", sample_quality, peak (out_left), max_location (out_left));
    }
}

void
test_simple()
{
  printf ("basic note tests\n");
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
      exit (1);
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

  printf (" - 100Hz freq zcross: %f\n", freq);

  expect_volume = pow (100. / 127, 2) / sqrt (2);

  volume = peak (out_left);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 0.001);

  volume = peak (out_right);
  vdiff_percent = 100 * fabs (volume - expect_volume) / expect_volume;
  assert (vdiff_percent < 0.001);

  printf (" - 100Hz freq volume: %f (expect %f)\n", volume, expect_volume);

  synth.all_sound_off();

  synth.add_event_note_on (0, 0, 48, 127);
  synth.process (outputs, sample_rate);

  freq = freq_from_zero_crossings (out_left, sample_rate);
  assert (freq > 49 && freq < 51);

  freq = freq_from_zero_crossings (out_right, sample_rate);
  assert (freq > 49 && freq < 51);

  printf (" - 50Hz freq zcross: %f\n", freq);

  printf ("panning\n");
  write_sfz ("<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440 volume_cc7=0 /* disable CC7 */");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.add_event_note_on (0, 0, 60, 127);
  synth.process (outputs, sample_rate);

  /* center panning - this is not exact as we have CC7=64 of 127 */
  expect_volume = 1 / sqrt (2);

  printf (" - center panning: %f %f (expect approx. %f)\n", peak (out_left), peak (out_right), expect_volume);

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

  printf (" - left panning: %f %f\n", peak (out_left), peak (out_right));

  vdiff_percent = 100 * fabs (peak (out_left) - 1);
  assert (vdiff_percent < 0.001);

  vdiff_percent = 100 * fabs (peak (out_right));
  assert (vdiff_percent < 0.001);

  /* right panning */
  synth.add_event_cc (0, 0, 10, 127);
  synth.process (outputs, sample_rate); // this takes a bit until it affects our note
  synth.process (outputs, sample_rate);

  printf (" - right panning: %f %f\n", peak (out_left), peak (out_right));
  vdiff_percent = 100 * fabs (peak (out_left));
  assert (vdiff_percent < 0.001);

  vdiff_percent = 100 * fabs (peak (out_right) - 1);
  assert (vdiff_percent < 0.001);

  printf ("volume via lfo\n");
  write_sfz ("<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440 "
             "lfo1_volume=-6.02 lfo1_wave=3 lfo1_freq=1");

  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.add_event_note_on (0, 0, 60, 127);
  for (int i = 0; i < 3; i++)
    {
      synth.process (outputs, sample_rate);
      vector<float> lvol = cut_ms (out_left, 100, 400, sample_rate);
      vector<float> hvol = cut_ms (out_left, 600, 900, sample_rate);
      double f = peak (hvol) / peak (lvol);
      printf (" - peak %f %f %f\n", peak (lvol), peak (hvol), peak (hvol) / peak (lvol));
      assert (f >= 1.999 && f <= 2.001);
    }
  printf ("pitch via lfo\n");
  write_sfz ("<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440 "
             "lfo1_pitch=1200 lfo1_wave=3 lfo1_freq=1");

  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.add_event_note_on (0, 0, 60, 127);
  for (int i = 0; i < 3; i++)
    {
      synth.process (outputs, sample_rate);
      vector<float> hfreq = cut_ms (out_left, 100, 400, sample_rate);
      vector<float> lfreq = cut_ms (out_left, 600, 900, sample_rate);
      double hf = freq_from_zero_crossings (hfreq, sample_rate);
      double lf = freq_from_zero_crossings (lfreq, sample_rate);
      double f = hf / lf;
      printf (" - freq %f %f %f\n", lf, hf, hf / lf);
      assert (lf >= 98 && lf <= 102);
      assert (hf >= 198 && hf <= 202);
      assert (f >= 1.98 && f <= 2.02);
    }
}

int
main()
{
  test_simple();
  test_interp_time_align();
  test_tiny_loop();

  unlink ("testsynth.sfz");
  unlink ("testsynth.wav");
}
