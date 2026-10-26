// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <cmath>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include <sndfile.h>
#include <array>
#include <vector>
#include <algorithm>

#include "liquidsfz.hh"
#include "log.hh"
#include "utils.hh"
#include "config.h"
#include "samplecache.hh"
#include "testutils.hh"

#if HAVE_FFTW
#include <fftw3.h>
#endif

using std::vector;
using std::string;
using std::max;
using LiquidSFZ::Synth;
using LiquidSFZInternal::string_printf;

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

inline double
window_gaussian (double x, double alpha = 3.0)
{
  if (fabs (x) > 1)
    return 0;

  // Gaussian function: exp(-0.5 * (x / sigma)^2)
  // We replace 1/sigma with alpha.
  return exp(-0.5 * pow (alpha * x, 2));
}

vector<float>
fft (vector<float>& in)
{
  // Include every complex output bin, including Nyquist for even input sizes.
  vector<float> out (2 * (in.size() / 2 + 1));
#if HAVE_FFTW
  auto plan_fft = fftwf_plan_dft_r2c_1d (in.size(), in.data(), (fftwf_complex *) out.data(), FFTW_ESTIMATE | FFTW_PRESERVE_INPUT);

  fftwf_execute_dft_r2c (plan_fft, in.data(), (fftwf_complex *) out.data());

  // usually we should keep the plan, but for this simple test program, the fft
  // is only computed once, so we can destroy the plan here
  fftwf_destroy_plan (plan_fft);
#endif
  return out;
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

  vector<float> fft_values = fft (padded_signal);

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
write_sample (const vector<float>& samples, int rate, int channels = 1, int loop_start = -1, int loop_end = -1)
{
  SF_INFO sfinfo = {0,};
  sfinfo.samplerate = rate;
  sfinfo.channels = channels;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  SNDFILE *sndfile = sf_open ("testsynth.wav", SFM_WRITE, &sfinfo);
  assert (sndfile);

  if (loop_start >= 0 && loop_end >= 0)
    {
      SF_INSTRUMENT inst{};

      inst.loop_count = 1;
      inst.loops[0].mode = SF_LOOP_FORWARD;
      inst.loops[0].start = loop_start;
      inst.loops[0].end   = loop_end;
      inst.loops[0].count = 0;      // infinite loop

      // Apply instrument chunk (writes SMPL chunk in WAV)
      int r = sf_command (sndfile, SFC_SET_INSTRUMENT, &inst, sizeof(inst));
      assert (r == SF_TRUE);
    }
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

SineDetectPartial
max_partial (const vector<float>& samples, int sample_rate)
{
  auto partials = sine_detect (sample_rate, samples);
  std::sort (partials.begin(), partials.end(), [](auto a, auto b) { return a.mag > b.mag; });
  if (partials.size())
    return partials[0];
  else
    return { -1, -1 };
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
              assert (db (partials[1].mag) < amag_max);
            }
        }
    }
#endif
}

void
test_wav_loop()
{
#if HAVE_FFTW
  printf ("wav loop tests:\n");
  constexpr int sample_rate = 44100;
  int offset = 42;
  vector<float> samples (200, 1); /* fill with one samples */

  for (int i = 0; i < 100; i++)
    samples[i + offset] = sin (i * 2 * M_PI / 100);

  auto check_pure_sine = [&] (int start, int end, bool expect_pure)
    {
      write_sample (samples, sample_rate, 1, start, end);
      write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 offset=99");

      Synth synth;
      synth.set_sample_rate (sample_rate);
      synth.set_live_mode (false);
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_gain (sqrt(2));
      for (int quality = 1; quality <= 3; quality++)
        {
          synth.all_sound_off();
          synth.set_sample_quality (quality);
          synth.add_event_note_on (0, 0, 60, 127);

          vector<float> out_left (sample_rate), out_right (sample_rate);
          float *outputs[2] = { out_left.data(), out_right.data() };
          synth.process (outputs, sample_rate);

          auto partials = sine_detect (sample_rate, out_left);
          bool pure = partials.size() == 1 && fabs (partials[0].mag - 1) < 0.01;

          printf (" - pure sine test: loop %d .. %d, quality %d, pure=%s (expect pure=%s)\n",
                  start, end, quality, pure ? "true" : "false", expect_pure ? "true" : "false");
          assert (pure == expect_pure);
        }
    };
  check_pure_sine (offset, offset + 101, false);
  check_pure_sine (offset, offset + 100, true);
  check_pure_sine (offset - 1, offset + 100, false);

  printf ("loop_count tests:\n");
  auto check_loop_count = [&] (int start, int end, int loop_count)
    {
      write_sample (samples, sample_rate, 1, start, end);
      write_sfz (string_printf ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 loop_count=%d", loop_count));
      Synth synth;
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_sample_rate (sample_rate);
      synth.set_live_mode (false);
      synth.set_gain (sqrt(2));
      synth.all_sound_off();
      synth.set_sample_quality (2);
      synth.add_event_note_on (0, 0, 60, 127);

      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };
      synth.process (outputs, sample_rate);

      int len = 0;
      float err = 0;
      for (int i = 0; i < int (out_left.size()); i++)
        {
          float expect;
          if (i < offset)
            expect = 1;
          else if (i < offset + 100 * (loop_count + 1))
            expect = sin ((i - offset) * 2 * M_PI / 100);
          else if (i < 100 * (loop_count + 2))
            expect = 1;
          else
            expect = 0;

          err = max (fabs (out_left[i] - expect), err);
          err = max (fabs (out_right[i] - expect), err);
          if (out_left[i] > 0.5)
            len = i + 1;
        }
      printf (" - loop_count=%d, err=%.2g (expect 0), len=%d (expect %d)\n", loop_count, err, len, (loop_count + 2) * 100);
      assert (err < 1e-6);
      assert (len == (loop_count + 2) * 100);
    };
  for (int loop_count : { 0, 1, 2, 3, 10 })
    check_loop_count (offset, offset + 100, loop_count);
#endif
}

void
test_pitch()
{
#if HAVE_FFTW
  int samples_sample_rate = 48000;
  vector<float> samples (100);
  for (int i = 0; i < 100; i++)
    samples[i] = sin (i * 2 * M_PI / 100);

  write_sample (samples, samples_sample_rate);
  write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 loop_mode=loop_continuous loop_start=0 loop_end=99 pitch_oncc100=1200");

  int sample_rate = 44100;
  Synth synth;
  synth.set_sample_rate (sample_rate);
  synth.set_live_mode (false);
  synth.set_gain (sqrt (2));
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  printf ("test pitch using cc\n");
  for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
    {
      synth.all_sound_off();
      synth.set_sample_quality (sample_quality);
      synth.add_event_cc (0, 0, 100, 0);
      synth.add_event_note_on (0, 0, 60, 127);

      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };
      synth.process (outputs, sample_rate);

      auto compare_max_partial = [&] (int cc) {
        double freq_expect = 480 * pow (2, cc / 127.);
        auto partial = max_partial (out_left, sample_rate);
        printf ("  - quality %d, freq cc=%03d: %.4f (expect %.4f), mag %.4f\n", sample_quality, cc, partial.freq, freq_expect, partial.mag);

        assert (fabs (partial.freq - freq_expect) < 0.01);
        assert (fabs (partial.mag - 1) < 0.001);
      };
      compare_max_partial (0);

      synth.add_event_cc (0, 0, 100, 64);
      synth.process (outputs, sample_rate * 0.1); // skip smoothing
      synth.process (outputs, sample_rate);

      compare_max_partial (64);

      synth.add_event_cc (0, 0, 100, 127);
      synth.process (outputs, sample_rate * 0.1); // skip smoothing
      synth.process (outputs, sample_rate);

      compare_max_partial (127);
    }
  write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 loop_mode=loop_continuous loop_start=0 loop_end=99 pitch=200 bend_up=500 bend_down=-700");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  printf ("simple pitch, pitch bend\n");
  for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
    {
      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };

      auto cmp = [&] (int cent) {
        auto partial = max_partial (out_left, sample_rate);
        float xfreq = 480 * pow (2, cent / 1200.);
        printf (" - quality %d, freq %.4f (expect %.4f), mag %.4f\n", sample_quality, partial.freq, xfreq, partial.mag);
        assert (fabs (partial.freq - xfreq) < 0.1);
        assert (fabs (partial.mag - 1) < 0.001);
      };

      synth.all_sound_off();
      synth.set_sample_quality (sample_quality);
      synth.add_event_pitch_bend (0, 0, 8192);
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, sample_rate);
      cmp (200);

      synth.add_event_pitch_bend (0, 0, 16383);
      synth.process (outputs, sample_rate * 0.1); // skip smoothing
      synth.process (outputs, sample_rate);
      cmp (200 + 500);

      synth.add_event_pitch_bend (0, 0, 0);
      synth.process (outputs, sample_rate * 0.1); // skip smoothing
      synth.process (outputs, sample_rate);
      cmp (200 - 700);
    }
  write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 loop_mode=loop_continuous loop_start=0 loop_end=99 pitch_veltrack=1200 amp_veltrack=0");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  printf ("pitch_veltrack:\n");
  auto pitch_veltrack_check = [&] (int velocity)
    {
      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };

      synth.set_sample_quality (3);
      synth.all_sound_off();
      synth.add_event_note_on (0, 0, 60, velocity);
      synth.process (outputs, sample_rate);
      auto partial = max_partial (out_left, sample_rate);
      double expect = 480 *  exp2 (velocity / 127.);
      printf (" - velocity %d -> got %f (expect %f)\n", velocity, partial.freq, expect);
      assert (fabs (partial.freq - expect) < 1e-4);
    };
  pitch_veltrack_check (1);
  pitch_veltrack_check (100);
  pitch_veltrack_check (127);
  printf ("octave_offest:\n");
  auto octave_offset_check = [&] (int octave_offset, int note, float expect_freq)
    {
      write_sfz (string_printf ("<control>octave_offset=%d <region>sample=testsynth.wav "
                                "volume_cc7=0 pan_cc10=0 loop_mode=loop_continuous loop_start=0 loop_end=99", octave_offset));
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };

      synth.set_sample_quality (3);
      synth.all_sound_off();
      synth.add_event_note_on (0, 0, note, 100);
      synth.process (outputs, sample_rate);
      auto partial = max_partial (out_left, sample_rate);
      printf (" - octave_offset %d, note %d, freq %f\n", octave_offset, note, partial.freq);
      assert (fabs (partial.freq - expect_freq) < 1e-4);
    };
  octave_offset_check (0, 60, 480);
  octave_offset_check (0, 72, 960);
  /* shift one octave down */
  octave_offset_check (1, 60, 240);
  octave_offset_check (1, 72, 480);
  printf ("sine generator (sine*) pitch:\n");
  auto sine_gen_pitch_check = [&] (const string& s, int note, float expect_freq)
    {
      write_sfz ("<region>sample=*sine " + s);
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };

      synth.all_sound_off();
      synth.add_event_note_on (0, 0, note, 100);
      synth.process (outputs, sample_rate);
      auto partial = max_partial (out_left, sample_rate);
      printf (" - %-44s--> note %d, freq %.4f (expect %.4f)\n", s.c_str(), note, partial.freq, expect_freq);
      double delta = 0.0005 * expect_freq; // ~= cent resolution
      assert (fabs (partial.freq - expect_freq) < delta);
    };
  double c_60_freq = 261.625565300599;
  sine_gen_pitch_check ("", 69, 440);
  sine_gen_pitch_check ("", 57, 220);
  sine_gen_pitch_check ("", 60, c_60_freq);
  sine_gen_pitch_check ("pitch_keytrack=50", 60 + 24, 2 * c_60_freq);
  sine_gen_pitch_check ("pitch_keytrack=0", 80, c_60_freq);
  sine_gen_pitch_check ("pitch_keycenter=83 pitch_keytrack=0 tune=21", 69, 1000);
  sine_gen_pitch_check ("pitch_keycenter=83 pitch_keytrack=0 tune=21", 60, 1000);
  sine_gen_pitch_check ("pitch_keycenter=83 tune=21", 83, 1000);
  sine_gen_pitch_check ("pitch_keycenter=83 tune=21", 95, 2000);
  printf ("sine generator (sine*) precision:\n");
  auto sine_gen_fft_check = [&] ()
    {
      write_sfz ("<region>sample=*sine volume_cc7=0 pan_cc10=0");
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_gain (sqrt (2));
      vector<float> out_left (sample_rate), out_right (sample_rate);
      float *outputs[2] = { out_left.data(), out_right.data() };

      synth.all_sound_off();
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, sample_rate);

      auto p = max_partial (out_left, sample_rate);
      printf (" - partial freq %f\n", p.freq);
      assert (fabs (p.freq - c_60_freq) < 1e-4);
      printf (" - partial mag %f\n", p.mag);
      assert (fabs (p.mag - 1) < 1e-5);

      constexpr double MIN_PADDING = 4;

      size_t padded_length = 2;
      while (out_left.size() * MIN_PADDING >= padded_length)
        padded_length *= 2;

      vector<float> padded_signal;
      float window_weight = 0;
      for (size_t i = 0; i < out_left.size(); i++)
        {
          const float w = window_gaussian ((i - out_left.size() * 0.5) / (out_left.size() * 0.5), 5);
          window_weight += w;
          padded_signal.push_back (out_left[i] * w);
        }
      padded_signal.resize (padded_length);

      vector<float> fft_values = fft (padded_signal);

      vector<float> mag_values;
      double main = 0, side = 0;
      for (size_t i = 0; i < fft_values.size(); i += 2)
        {
          double freq = double (i) * sample_rate / (2 * fft_values.size());
          double mag = sqrt (fft_values[i] * fft_values[i] + fft_values[i + 1] * fft_values[i + 1]) * (2 / window_weight);
          if (fabs (freq - c_60_freq) < 10)
            main = max (mag, main);
          else
            side = max (mag, side);
        }
      printf (" - main lobe: %f\n", db (main));
      assert (fabs (db (main) - 0) < 0.005);
      printf (" - side lobe: %f\n", db (side));
      assert (db (side) < -120);
    };
  sine_gen_fft_check();
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
      printf (" - freq/zcr %f %f %f\n", lf, hf, hf / lf);
      assert (lf >= 98 && lf <= 102);
      assert (hf >= 198 && hf <= 202);
      assert (f >= 1.98 && f <= 2.02);
#if HAVE_FFTW
      hf = max_partial (hfreq, sample_rate).freq;
      lf = max_partial (lfreq, sample_rate).freq;
      f = hf / lf;
      printf (" - freq/fft %f %f %f\n", hf, lf, f);
      assert (fabs (lf - 100) < 0.01);
      assert (fabs (hf - 200) < 0.01);
      assert (fabs (f - 2) < 0.0001);
#endif
    }

  printf ("volume test:\n");
  auto chk_vol = [&] (const string& s, double expect)
    {
      write_sfz (s + " sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440"
                     " volume_cc7=0 pan_cc10=0");
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_gain (sqrt (2));
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, sample_rate);

      double db_diff = db (peak (out_left));
      printf (" - %s: expected db %.1f, got db %.1f\n", s.c_str(), expect, db_diff);
      assert (fabs (expect - db_diff) < 1e-4);
    };
  chk_vol ("<group>group_volume=10 <region>volume=0", 10);
  chk_vol ("<master>master_volume=6 <region>volume=0", 6);
  chk_vol ("<global>global_volume=4 <region>volume=-3", 1);
  chk_vol ("<global>global_volume=2 <master>master_volume=3 <group>group_volume=4 <region>volume=5", 2 + 3 + 4 + 5);

  printf ("phase test:\n");
  write_sfz ("<group>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440"
             " volume_cc7=0 pan_cc10=0<region>volume=1<region>phase=invert");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.set_gain (sqrt (2));
  synth.add_event_note_on (0, 0, 60, 127);
  synth.process (outputs, sample_rate);

  double phase_peak = peak (out_left);
  double expect_phase_peak = db_to_factor (1) - 1;
  printf (" - peak=%f expect=%f\n", phase_peak, expect_phase_peak);
  assert (fabs (phase_peak - expect_phase_peak) < 1e-6);

  printf ("ext cc test:\n");
  auto chk_xcc = [&] (const string& s, int note, int vel, double expect, bool cmp = true)
    {
      write_sfz ("<group>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440"
                 " volume_cc7=0 pan_cc10=0 amp_veltrack=0<region>" + s);
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_gain (sqrt (2));
      synth.add_event_note_on (0, 0, note, vel);
      synth.process (outputs, sample_rate);

      double peak_db = db (peak (out_left));
      if (cmp)
        {
          printf (" - %s: expected db %.2f, got db %.2f\n", s.c_str(), expect, peak_db);
          assert (fabs (expect - peak_db) < 1e-3);
        }
      return peak_db;
    };
  chk_xcc ("volume_oncc131=6", 60, 127, 6);
  chk_xcc ("volume_oncc131=6", 60, 64, 6.0 * 64 / 127);
  chk_xcc ("volume_oncc133=6", 64, 127, 6.0 * 64 / 127);
  chk_xcc ("volume_oncc133=6", 32, 127, 6.0 * 32 / 127);
  double v135_min = 10, v135_max = -10, v136_min = 10, v136_max = -10;
  for (int i = 0; i < 100; i++)
    {
      double v135 = chk_xcc ("volume_oncc135=6", 60, 127, 0, false);
      v135_min = std::min (v135, v135_min);
      v135_max = std::max (v135, v135_max);
      double v136 = chk_xcc ("volume_oncc136=6", 60, 127, 0, false);
      v136_min = std::min (v136, v136_min);
      v136_max = std::max (v136, v136_max);
    }
  printf (" - random unipolar %f..%f\n", v135_min, v135_max);
  assert (v135_min > -0.01 && v135_min < 1);
  assert (v135_max > 5 && v135_max < 6.01);
  printf (" - random bipolar %f..%f\n", v136_min, v136_max);
  assert (v136_min > -6.01 && v136_min < -4);
  assert (v136_max > 4 && v136_max < 6.01);
  printf ("silence trigger release test:\n");
  for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
    {
      write_sfz ("<region>trigger=attack sample=*silence"
                 "<region>trigger=release sample=testsynth.wav");
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_sample_quality (sample_quality);
      synth.set_gain (sqrt (2));
      synth.add_event_note_on (0, 0, 60, 127);
      synth.add_event_note_off (1000, 0, 60);
      synth.process (outputs, sample_rate);
      int min_pos = out_left.size(), max_pos = -1;
      for (int i = 0; i < int (out_left.size()); i++)
        {
          float eps;
          if (sample_quality == 3)
            eps = 0.005;
          else
            eps = 0;
          if (fabs (out_left[i]) > eps)
            {
              min_pos = std::min (i, min_pos);
              max_pos = std::max (i, max_pos);
            }
        }
      printf (" - quality %d min_pos=%d (expect 1001) max_pos=%d (expect 1440)\n", sample_quality, min_pos, max_pos);
      assert (min_pos == 1001 && max_pos == 1440);
    }
  printf ("*noise test:\n");
  write_sfz ("<region>sample=*noise volume_cc7=0 pan_cc10=0");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.set_sample_quality (3);
  synth.set_gain (sqrt (2));
  synth.add_event_note_on (0, 0, 60, 127);
  synth.process (outputs, sample_rate);
  float min_rnd = 2, max_rnd = -2, avg_rnd = 0;
  for (auto f : out_left)
    {
      min_rnd = std::min (min_rnd, f);
      max_rnd = std::max (max_rnd, f);
      avg_rnd += f;
    }
  avg_rnd /= sample_rate;
  printf (" - min_rnd %f (expect ~= -1)\n", min_rnd);
  printf (" - max_rnd %f (expect ~= 1)\n", max_rnd);
  printf (" - avg_rnd %f (expect ~= 0)\n", avg_rnd);
  assert (avg_rnd > -0.03 && avg_rnd < 0.03);
  assert (max_rnd > 0.999 && max_rnd < 1.000001);
  assert (min_rnd < -0.999 && min_rnd > -1.000001);

  /* 2 <control> sections: values should be merged, not overwritten */
  write_sfz ("<control>set_cc100=127<control>set_cc101=127<region>sample=testsynth.wav lokey=20 hikey=100 loop_mode=loop_continuous loop_start=0 loop_end=440"
             " volume_cc7=0 pan_cc10=0 amp_veltrack=0 amplitude_oncc100=100 amplitude_oncc101=100");
  if (!synth.load ("testsynth.sfz"))
    {
      fprintf (stderr, "parse error: exiting\n");
      exit (1);
    }
  synth.all_sound_off();
  synth.set_gain (sqrt (2));
  synth.add_event_note_on (0, 0, 60, 100);
  synth.process (outputs, sample_rate);

  double peak_db = db (peak (out_left));
  printf ("2control test:\n");
  printf (" - got db %.2f (expect 0)\n", peak_db);
  assert (fabs (peak_db) < 1e-3);
}

void
test_width()
{
#if HAVE_FFTW
  int sample_rate = 44100;
  vector<float> samples;

  for (int i = 0; i < sample_rate; i++)
    {
      samples.push_back (sin (i * 2 * M_PI * 440 / sample_rate));
      samples.push_back (sin (i * 2 * M_PI * 1000 / sample_rate));
    }

  write_sample (samples, sample_rate, 2);

  Synth synth;
  synth.set_sample_rate (sample_rate);
  synth.set_live_mode (false);

  vector<float> out_left (sample_rate), out_right (sample_rate);
  float *outputs[2] = { out_left.data(), out_right.data() };
  auto get_partial = [&] (vector<float> samples, double f)
    {
      auto partials = sine_detect (sample_rate, samples);

      for (auto p : partials)
        if (fabs (p.freq - f) < 10)
          return p.mag;
      return 0.0;
    };
  printf ("width test:\n");
  auto width_test = [&] (double width, double xl440, double xr440, double xl1000, double xr1000)
    {
      write_sfz (string_printf ("<region>width=%f sample=testsynth.wav lokey=20 hikey=100 volume_cc7=0 pan_cc10=0", width));
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }
      synth.set_gain (sqrt (2));
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, sample_rate);
      double l440 = get_partial (out_left, 440);
      double r440 = get_partial (out_right, 440);
      double l1000 = get_partial (out_left, 1000);
      double r1000 = get_partial (out_right, 1000);
      printf (" - width %4.0f l440=%.2f r440=%.2f l1000=%.2f r1000=%.2f\n", width, l440, r440, l1000, r1000);
      assert (fabs (l440 - xl440) < 0.01);
      assert (fabs (r440 - xr440) < 0.01);
      assert (fabs (l1000 - xl1000) < 0.01);
      assert (fabs (r1000 - xr1000) < 0.01);
    };
  width_test (200,  1.5,  0.5,  0.5,  1.5);
  width_test (100,  1,    0,    0,    1);
  width_test (50,   0.75, 0.25, 0.25, 0.75);
  width_test (0,    0.5,  0.5,  0.5,  0.5);
  width_test (-100, 0,    1,    1,    0);
#endif
}

void
test_end()
{
  int sample_rate = 44100;
  vector<float> samples;
  for (int i = 0; i < 1000; i++)
    samples.push_back (1);

  write_sample (samples, sample_rate);

  vector<float> out_left (sample_rate), out_right (sample_rate);
  float *outputs[2] = { out_left.data(), out_right.data() };

  Synth synth;
  synth.set_sample_rate (sample_rate);
  synth.set_live_mode (false);
  printf ("test end opcode:\n");
  for (int sample_quality = 1; sample_quality <= 3; sample_quality++)
    {
      for (int try_offset : { 0, 500 })
        {
          for (int try_end : { -1, 0, 1, 5, 100 })
            {
              write_sfz (string_printf ("<region>sample=testsynth.wav lokey=20 hikey=100 offset=%d end=%d", try_offset, try_offset + try_end));
              if (!synth.load ("testsynth.sfz"))
                {
                  fprintf (stderr, "parse error: exiting\n");
                  exit (1);
                }
              synth.set_sample_quality (sample_quality);
              synth.add_event_note_on (0, 0, 60, 127);
              synth.process (outputs, sample_rate);

              int end_l = -1;
              int end_r = -1;
              for (size_t i = 0; i < out_left.size(); i++)
                {
                  // at quality 3, the interpolation will produce ripple after the end
                  float threshold = (sample_quality == 3) ? 0.05 : 0;

                  if (out_left[i] > threshold)
                    end_l = i;
                  if (out_right[i] > threshold)
                    end_r = i;
                }
              /* we skip regions with end == 0 or end == -1 completely on playback */
              int expect = (try_offset == 0 && try_end == 0) ? -1 : try_end;
              printf (" - quality %d - got end %d, %d expect %d\n", sample_quality, end_l, end_r, expect);
              assert (end_l == expect);
              assert (end_r == expect);
            }
        }
    }
}

void
test_filter()
{
  printf ("test eq opcode:\n");

  int sample_rate = 48000;
  vector<float> freq, samples;

  /* sincos sweep */
  double phase = 0;
  double l = sample_rate * 5;
  double factor = pow (sample_rate / 2 / 20., (1./l));
  double vol = 0;
  for (double f = 20; f < sample_rate / 2; f *= factor)
    {
      freq.push_back (f);
      samples.push_back (sin (phase) * vol);
      samples.push_back (cos (phase) * vol);
      phase += f / sample_rate * 2 * M_PI;
      vol += 1. / 500; /* avoid click at start */
      if (vol > 1)
        vol = 1;
    }

  write_sample (samples, sample_rate, 2);
  auto chk_eq = [&] (float eq_freq, float eq_bw, float eq_gain)
    {
      write_sfz (string_printf("<region>sample=testsynth.wav lokey=20 hikey=100 eq1_freq=%f eq1_bw=%f eq1_gain=%f volume_cc7=0 pan_cc10=0",
                               eq_freq, eq_bw, eq_gain));

      Synth synth;
      synth.set_sample_rate (sample_rate);
      synth.set_live_mode (false);
      if (!synth.load ("testsynth.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          exit (1);
        }

      vector<float> out_left (l), out_right (l);
      float *outputs[2] = { out_left.data(), out_right.data() };
      synth.set_gain (sqrt (2));
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, l);

      float peak_freq = 0, peak_mag_db = 0, lfreq = 0, hfreq = 0, lmag_db = 0, hmag_db = 0;
      for (size_t i = 0; i < out_left.size(); i++)
        {
          float mag_db = db (sqrt (out_left[i] * out_left[i] + out_right[i] * out_right[i]));
          if (mag_db > peak_mag_db)
            {
              peak_freq = freq[i];
              peak_mag_db = mag_db;
            }
          if (freq[i] < eq_freq && fabs (mag_db - eq_gain / 2) < fabs (lmag_db - eq_gain / 2))
            {
              lfreq = freq[i];
              lmag_db = mag_db;
            }
          if (freq[i] > eq_freq && fabs (mag_db - eq_gain / 2) < fabs (hmag_db - eq_gain / 2))
            {
              hfreq = freq[i];
              hmag_db = mag_db;
            }
        }
      double bw = log2 (hfreq / lfreq);
      printf (" - peak_freq=%.1f (expect %.1f), peak_mag=%.2f (expect %.1f), bw=%.2f (expect %.1f)\n",
              peak_freq, eq_freq,
              peak_mag_db, eq_gain,
              bw, eq_bw);
      assert (fabs (peak_freq - eq_freq) < 2);
      assert (fabs (peak_mag_db - eq_gain) < 0.01);
      assert (fabs (bw - eq_bw) < 0.02);
    };
  chk_eq (440, 1, 6);
  chk_eq (1000, 2, 4);
}

void
test_fil_type()
{
  printf ("test fil_type opcode:\n");
  constexpr int sample_rate = 48000;
  constexpr int n_frames = 4800;
  const int frequencies[] = { 250, 1000, 4000 };
  // Steady-state gains in dB for cutoff=1000, resonance=0 (Q=1).
  // These are analytical responses with bilinear frequency warping; the
  // 4/6-pole filters cascade two/three identical 2-pole sections.
  const struct {
    const char *type;
    double gain_db[3];
  } cases[] = {
    { "lpf_1p", { -0.2626, -3.0103, -12.4828 } },
    { "hpf_1p", { -12.3154, -3.0103,  -0.2524 } },
    { "lpf_2p", {   0.2616,  0.0000, -24.2095 } },
    { "hpf_2p", { -23.8441,  0.0000,   0.2515 } },
    { "bpf_2p", { -11.7913,  0.0000, -11.9790 } },
    { "brf_2p", {  -0.2975, -160.00,  -0.2845 } },
    { "lpf_4p", {   0.5231,  0.0000, -48.4189 } },
    { "hpf_4p", { -47.6883,  0.0000,   0.5029 } },
    { "lpf_6p", {   0.7847,  0.0000, -72.6284 } },
    { "hpf_6p", { -71.5324,  0.0000,   0.7544 } },
  };

  for (int channels : { 1, 2 })
    for (int f = 0; f < 3; f++)
      {
        vector<float> samples;
        // Keep the sample end (and its fade-out) outside the measured audio.
        for (int i = 0; i < n_frames * 2; i++)
          for (int ch = 0; ch < channels; ch++)
            samples.push_back (0.5 * sin (2 * M_PI * frequencies[f] * i / sample_rate + ch * M_PI / 2));
        write_sample (samples, sample_rate, channels);

        auto render = [&] (const string& opcodes)
          {
            write_sfz ("<region>sample=testsynth.wav pitch_keycenter=60 volume_cc7=0 pan_cc10=0 " + opcodes);
            Synth synth;
            synth.set_sample_rate (sample_rate);
            synth.set_live_mode (false);
            const bool loaded = synth.load ("testsynth.sfz");
            assert (loaded);
            vector<float> left (n_frames), right (n_frames);
            float *outputs[] = { left.data(), right.data() };
            synth.add_event_note_on (0, 0, 60, 127);
            synth.process (outputs, n_frames);
            std::array<double, 2> rms {};
            for (int ch = 0; ch < 2; ch++)
              {
                // Discard startup transients, then measure complete periods.
                for (int i = n_frames / 2; i < n_frames; i++)
                  {
                    assert (std::isfinite (outputs[ch][i]));
                    rms[ch] += double (outputs[ch][i]) * outputs[ch][i];
                  }
                rms[ch] = sqrt (rms[ch] / (n_frames / 2));
              }
            return rms;
          };

        const auto bypass = render ("");
        assert (bypass[0] > 0.1 && bypass[1] > 0.1);
        for (int slot : { 1, 2 })
          for (const auto& test : cases)
            {
              const auto rms = render (string_printf (slot == 1 ? "fil_type=%s cutoff=1000 resonance=0"
                                                                : "fil2_type=%s cutoff2=1000 resonance2=0", test.type));
              for (int ch = 0; ch < 2; ch++)
                {
                  const double gain_db = db (rms[ch] / bypass[ch]);
                  printf (" - %s slot=%d channels=%d channel=%d freq=%d gain=%.4f expect=%.4f\n",
                          test.type, slot, channels, ch, frequencies[f], gain_db, test.gain_db[f]);
                  // The notch is ideally zero; allow numerical roundoff there.
                  if (test.gain_db[f] == -160)
                    assert (gain_db < -80);
                  else
                    assert (fabs (gain_db - test.gain_db[f]) < 0.1);
                }
            }
      }
}

void
test_unsupported_peq_filter()
{
  // #56, F11: PEQ is supported by eqN_*, not by the regular filter slots.
  printf ("test unsupported peq filter type\n");
  auto render = [] (const string& params, int expected_warnings)
    {
      write_sfz ("<region>sample=*sine " + params);
      Synth synth;
      int warnings = 0;
      synth.set_log_function ([&] (LiquidSFZ::Log level, const char *message)
        {
          if (level == LiquidSFZ::Log::WARNING && strstr (message, "unsupported filter type: peq"))
            warnings++;
        });
      const bool loaded = synth.load ("testsynth.sfz");
      assert (loaded);
      vector<float> left (1024), right (1024);
      float *out[] = { left.data(), right.data() };
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (out, left.size());
      assert (warnings == expected_warnings);
      for (size_t i = 0; i < left.size(); i++)
        assert (std::isfinite (left[i]) && std::isfinite (right[i]));
      left.insert (left.end(), right.begin(), right.end());
      return left;
    };

  for (const string& eq : { string(), string ("eq1_freq=1000 eq1_bw=1 eq1_gain=6 ") })
    {
      const auto expected = render (eq, 0);
      for (const char *filter : { "fil_type=peq cutoff=1000", "fil2_type=peq cutoff2=1000" })
        {
          const auto actual = render (eq + filter, 1);
          assert (actual == expected);
        }
    }
}

void
test_grow_voices_with_lfo()
{
  // https://github.com/swesterfeld/liquidsfz/issues/56
  // LFOGen stores a pointer to its owning Voice. Growing the voice vector must
  // preserve that relationship when existing Voice objects are relocated.
  printf ("test growing voice pool with velocity-modulated LFO\n");
  write_sfz ("<region>sample=*sine lfo1_wave=3 lfo1_freq=0 lfo1_volume_oncc131=6");

  Synth synth;
  const bool loaded = synth.load ("testsynth.sfz");
  assert (loaded);

  constexpr uint n_frames = 64;
  float expected_left[n_frames], expected_right[n_frames];
  float actual_left[n_frames], actual_right[n_frames];
  float *expected[] = { expected_left, expected_right };
  float *actual[] = { actual_left, actual_right };

  auto render = [&] (float **outputs)
    {
      // The free list is consumed backwards: after growth the first voice is
      // newly constructed, but the second was relocated. Use separate channels
      // so the second note does not release the first one.
      // CC131 is note-on velocity. Different velocities make the relocated
      // voice observably use the wrong owner even without ASan: its stale
      // pointer refers to the first voice from the baseline render.
      synth.add_event_note_on (0, 0, 60, 50);
      synth.add_event_note_on (0, 1, 60, 127);
      synth.process (outputs, n_frames);
      assert (synth.active_voice_count() == 2);
    };

  render (expected);
  synth.set_max_voices (synth.max_voices() + 1);
  // With the bug, CC131 dereferences the relocated voice's stale owner pointer
  // in LFOGen::update_ccs(). ASan reports a heap-use-after-free here.
  render (actual);

  for (uint i = 0; i < n_frames; i++)
    {
      assert (std::isfinite (actual_left[i]) && std::isfinite (actual_right[i]));
      assert (fabs (actual_left[i] - expected_left[i]) < 1e-6);
      assert (fabs (actual_right[i] - expected_right[i]) < 1e-6);
    }
}

void
test_lfo_phase_wrapping()
{
  // F09: malformed frequencies must never trap the audio thread in a wrap loop.
  printf ("test bounded LFO phase wrapping\n");
#if !LIQUIDSFZ_OS_WINDOWS
  alarm (10);
#endif

  auto render = [] (const string& params)
    {
      write_sfz ("<region>sample=*sine lfo1_volume=-6 " + params);
      Synth synth;
      synth.set_sample_rate (48000);
      const bool loaded = synth.load ("testsynth.sfz");
      assert (loaded);
      synth.add_event_note_on (0, 0, 60, 127);
      synth.add_event_cc (0, 0, 1, 127);
      vector<float> result;
      float left[64], right[64];
      float *out[] = { left, right };
      double energy = 0;
      for (int block = 0; block < 128; block++)
        {
          synth.process (out, 64);
          for (uint i = 0; i < 64; i++)
            {
              assert (std::isfinite (left[i]) && std::isfinite (right[i]));
              assert (fabs (left[i]) < 2 && fabs (right[i]) < 2);
              energy += left[i] * left[i];
              result.push_back (left[i]);
            }
        }
      assert (energy > 0.01);
      return result;
    };

  // Reversing a triangle is equivalent to shifting it by half a cycle.
  for (int freq : { 0, 5, 100 })
    {
      auto backward = render (string_printf ("lfo1_freq=%d", -freq));
      auto forward = render (string_printf ("lfo1_freq=%d lfo1_phase=0.5", freq));
      for (size_t i = 0; i < backward.size(); i++)
        assert (fabs (backward[i] - forward[i]) < 1e-4);
    }

  for (int wave : { 0, 1, 6, 7 })
    for (const char *freq : { "1e20", "-1e20", "3e38", "1e39", "-1e39" })
      render (string_printf ("lfo1_wave=%d lfo1_freq=%s", wave, freq));

  // Exercise controller sums and LFO-to-LFO modulation, including overflow.
  render ("lfo1_freq=3e38 lfo1_freq_oncc1=3e38");
  render ("lfo1_freq=5 lfo2_wave=3 lfo2_freq=0 lfo2_freq_lfo1=-100");
  render ("lfo2_wave=3 lfo2_freq=0 lfo2_freq_lfo1=1e39");
  render ("lfo2_wave=0 lfo2_freq=0 lfo2_freq_lfo1=1e39"); // 0 * infinity
  render ("lfo1_freq=5 lfo1_phase=1e39 lfo1_phase_oncc1=-1e39");
#if !LIQUIDSFZ_OS_WINDOWS
  alarm (0);
#endif
}

void
test_repro_lfo_alloc()
{
  // https://github.com/swesterfeld/liquidsfz/issues/56, F10
  printf ("test LFO alloc\n");

  write_sfz ("<region>sample=*sine key=60 lfo1_freq=1 lfo2_freq=5 lfo2_pitch=100 lfo1_freq_lfo2_oncc1=2");

  Synth synth;
  const bool loaded = synth.load ("testsynth.sfz");
  assert (loaded);

  constexpr uint n_frames = 64;
  float left[n_frames], right[n_frames];
  float *out[] = { left, right };

  synth.add_event_note_on (0, 0, 60, 100);
  synth.process (out, n_frames);
  synth.add_event_cc (0, 0, 1, 127);
  synth.process (out, n_frames);
}

void
test_bank_definitions()
{
  printf ("test bank definitions\n");
  write_sample (vector<float> (1024, 0.5f), 44100, 1);
  std::ofstream ("testsynth-include.sfz") << "<region> sample=testsynth.wav\n";

  for (bool include : { false, true })
    {
      write_sfz (include ? "#include \"$empty$file\"\n" : "<region> sample=$empty$file\n");
      for (const string& definition : { string ("<Define name=\"\" value=\"\"/>"),
                                       string ("<Define value=\"\"/>"),
                                       string ("<Define name=\"\" value=\"x\"/>"),
                                       string ("") })
        {
          std::ofstream ("testsynth.bank.xml")
            << "<AriaBank>" << definition
            << "<Define name=\"$empty\" value=\"\"/>"
            << "<Define name=\"$file\" value=\""
            << (include ? "testsynth-include.sfz" : "testsynth.wav")
            << "\"/><AriaProgram name=\"Test\"><AriaElement path=\"testsynth.sfz\"/>"
            << "</AriaProgram></AriaBank>\n";

          Synth synth;
          const bool loaded = synth.load_bank ("testsynth.bank.xml");
          assert (loaded);
          if (loaded)
            {
              // Valid definitions still expand in both paths; empty values are legal.
              const bool selected = synth.select_program (0);
              assert (selected);
              synth.set_live_mode (false);
              synth.add_event_note_on (0, 0, 60, 100);
              float left[64], right[64];
              float *out[] = { left, right };
              synth.process (out, 64);
              assert (std::any_of (left, left + 64, [] (float x) { return x != 0; }));
            }
        }
    }
  unlink ("testsynth-include.sfz");
  unlink ("testsynth.bank.xml");
}

void
test_unload_playing_sample()
{
  // https://github.com/swesterfeld/liquidsfz/issues/56, F04
  printf ("test unloading a playing sample\n");
  write_sample (vector<float> (1024, 0.5f), 44100, 1);
  write_sfz ("<region>sample=testsynth.wav loop_mode=loop_continuous");

  FILE *bank_file = fopen ("testsynth.bank.xml", "w");
  assert (bank_file);
  fputs ("<AriaBank><AriaProgram name=\"Test\">"
         "<AriaElement path=\"testsynth.sfz\"/>"
         "</AriaProgram></AriaBank>\n", bank_file);
  fclose (bank_file);

  for (bool replace_bank : { true, false })
    {
      Synth synth;
      // Keep live mode enabled: offline rendering waits for the cache worker,
      // whose sample references can mask the lifetime bug.
      bool ok = synth.load_bank ("testsynth.bank.xml");
      assert (ok);
      ok = synth.select_program (0);
      assert (ok);

      float left[64], right[64];
      float *out[] = { left, right };
      synth.add_event_note_on (0, 0, 60, 100);
      synth.process (out, 64);
      assert (synth.active_voice_count() == 1);
      assert (std::any_of (left, left + 64, [] (float x) { return x != 0; }));

      // Unload immediately, before the cache worker normally discovers playback.
      if (replace_bank)
        {
          ok = synth.load_bank ("testsynth.bank.xml");
          assert (ok);
        }
      else
        {
          ok = synth.select_program (1);
          assert (!ok);
        }
      assert (synth.active_voice_count() == 0);
      synth.process (out, 64);
      for (uint i = 0; i < 64; i++)
        assert (left[i] == 0 && right[i] == 0);

      // Both paths retain a usable bank index.
      ok = synth.select_program (0);
      assert (ok);
      synth.add_event_note_on (0, 0, 60, 100);
      synth.process (out, 64);
      assert (synth.active_voice_count() == 1);
      assert (std::any_of (left, left + 64, [] (float x) { return x != 0; }));
    }
  unlink ("testsynth.bank.xml");
}

void
test_filesystem_load_errors()
{
  // #56, F20: filesystem lookup failures must not escape the loading API.
  printf ("test filesystem load errors\n");
  namespace fs = std::filesystem;
  const auto root = LiquidSFZTest::create_temp_directory ("liquidsfz-paths-");
  std::ofstream (root / "file") << "not a directory\n";
  std::ofstream (root / "Valid.sfz") << "<region>sample=*sine\n";
  vector<string> bad_paths = { "missing", "file/child" };

  // Symlinks are not available on every filesystem/platform.
  std::error_code ec;
  fs::create_symlink ("loop", root / "loop", ec);
  if (!ec)
    bad_paths.push_back ("loop");
  fs::create_symlink ("missing", root / "dangling", ec);
  if (!ec)
    bad_paths.push_back ("dangling");

  fs::create_directory (root / "blocked");
  fs::permissions (root / "blocked", fs::perms::none, ec);
  // Root or filesystems without Unix permissions may still allow access.
  fs::directory_iterator probe (root / "blocked", ec);
  if (ec == std::errc::permission_denied)
    bad_paths.push_back ("blocked/child");

  for (const auto& bad_path : bad_paths)
    {
      printf (" - %s\n", bad_path.c_str());
      const string filename = (root / bad_path).string();
      const string resolved = LiquidSFZInternal::path_resolve_case_insensitive (filename);
      assert (resolved == filename);

      Synth synth;
      string messages;
      synth.set_log_function ([&] (LiquidSFZ::Log, const char *message) { messages += message; });
      const string sfz = (root / "test.sfz").string();
      std::ofstream (sfz) << "<region>sample=" << bad_path << "\n";
      // Missing samples are warnings, so the instrument itself still loads.
      bool ok = synth.load (sfz);
      assert (ok);
      assert (messages.find ("missing sample: '" + filename + "'") != string::npos);

      messages.clear();
      std::ofstream (sfz) << "#include \"" << bad_path << "\"\n";
      ok = synth.load (sfz);
      assert (!ok);
      assert (messages.find ("unable to read #include '" + filename + "'") != string::npos);

      const string bank = (root / "test.bank.xml").string();
      std::ofstream (bank) << "<AriaBank><AriaProgram name=\"Test\"><AriaElement path=\""
                           << bad_path << "\"/></AriaProgram></AriaBank>\n";
      ok = synth.load_bank (bank);
      assert (ok); // Program files are opened when selected.
      ok = synth.select_program (0);
      assert (!ok);

      // The synth remains usable and case-insensitive lookup still works.
      std::ofstream (sfz) << "#include \"valid.sfz\"\n";
      ok = synth.load (sfz);
      assert (ok);
      float left[64], right[64];
      float *out[] = { left, right };
      synth.add_event_note_on (0, 0, 60, 100);
      synth.process (out, 64);
      assert (synth.active_voice_count() == 1);
    }
  fs::permissions (root / "blocked", fs::perms::owner_all);
  fs::remove_all (root);
}

void
test_velocity_zero_note_off()
{
  // #56, F13: velocity-zero note-on must apply octave_offset only once.
  printf ("test velocity-zero note-off with octave offsets\n");
  for (int octave_offset : { -1, 0, 1 })
    for (int key : { 12, 60, 115 })
      {
        write_sfz (string_printf ("<control>octave_offset=%d "
                                 "<region>sample=*sine ampeg_release=0.005", octave_offset));
        vector<float> reference;
        for (bool velocity_zero : { false, true })
          {
            Synth synth;
            synth.set_sample_rate (44100);
            const bool loaded = synth.load ("testsynth.sfz");
            assert (loaded);
            vector<float> left (2048), right (2048);
            float *out[] = { left.data(), right.data() };
            synth.add_event_note_on (0, 0, key, 100);
            synth.process (out, 64);
            assert (synth.active_voice_count() == 1);
            assert (std::any_of (left.begin(), left.begin() + 64, [] (float x) { return x != 0; }));

            if (velocity_zero)
              synth.add_event_note_on (17, 0, key, 0);
            else
              synth.add_event_note_off (17, 0, key);
            synth.process (out, left.size());
            assert (synth.active_voice_count() == 0);
            left.insert (left.end(), right.begin(), right.end());
            if (velocity_zero)
              assert (left == reference);
            else
              reference = left;
          }
      }
}

void
test_key_metadata_and_switches()
{
  printf ("test key metadata and switches\n");
  auto check_keys = [] (const string& regions, int first, int last, int switch_key = -1) {
    write_sfz (regions);
    Synth synth;
    const bool loaded = synth.load ("testsynth.sfz");
    assert (loaded);
    const auto keys = synth.list_keys();
    assert (keys.size() == size_t (last - first + 1));
    for (size_t i = 0; i < keys.size(); i++)
      {
        assert (keys[i].key() == first + int (i));
        assert (keys[i].is_switch() == (keys[i].key() == switch_key));
        if (keys[i].is_switch())
          assert (keys[i].label() == "Switch");
      }
  };
  check_keys ("<region>sample=*sine", 0, 127);
  check_keys ("<region>sample=*sine key=0", 0, 0);
  check_keys ("<region>sample=*sine key=127", 127, 127);
  check_keys ("<region>sample=*sine lokey=-10 hikey=2147483647", 0, 127);
  check_keys ("<region>sample=*sine lokey=128 hikey=2147483647", 0, -1);
  check_keys ("<region>sample=*sine lokey=10 hikey=9", 0, -1);
  check_keys ("<control>octave_offset=1 <region>sample=*sine", 12, 127);
  check_keys ("<control>octave_offset=-1 <region>sample=*sine", 0, 115);
  const string normal = "<region>sample=*sine";
  const string switched = "<region>sample=*sine sw_last=0 sw_label=Switch";
  check_keys (normal + switched, 0, 127, 0);
  check_keys (switched + normal, 0, 127, 0);

  // Large switch endpoints must produce only MIDI keys, without a long loop.
  write_sfz ("<region>sample=*sine sw_lolast=0 sw_hilast=2147483647");
  {
    Synth synth;
    const bool loaded = synth.load ("testsynth.sfz");
    assert (loaded);
    const auto keys = synth.list_keys();
    assert (keys.size() == 128);
    for (size_t i = 0; i < keys.size(); i++)
      {
        assert (keys[i].key() == int (i));
        assert (keys[i].is_switch());
      }
  }

  for (int octave_offset : { -1, 0, 1 })
    for (int switch_key : { 0, 12, 115, 127 })
      {
        const int external_switch = switch_key + octave_offset * 12;
        if (external_switch < 0 || external_switch > 127)
          continue;
        for (bool use_default : { false, true })
          {
            write_sfz (string_printf ("<control>octave_offset=%d "
                                     "<region>sample=*sine key=60 sw_lokey=0 sw_hikey=127 "
                                     "sw_last=%d sw_default=%d sw_label=Switch",
                                     octave_offset, switch_key, use_default ? switch_key : -1));
            Synth synth;
            const bool loaded = synth.load ("testsynth.sfz");
            assert (loaded);
            const auto keys = synth.list_keys();
            assert (keys.size() == 2);
            bool found_switch = false;
            for (const auto& key : keys)
              if (key.is_switch())
                {
                  assert (key.key() == external_switch);
                  assert (key.label() == "Switch");
                  found_switch = true;
                }
              else
                assert (key.key() == 60 + octave_offset * 12);
            assert (found_switch);

            float left[64], right[64];
            float *out[] = { left, right };
            const int note = 60 + octave_offset * 12;
            synth.add_event_note_on (0, 0, note, 100);
            synth.process (out, 64);
            assert (synth.active_voice_count() == (use_default ? 1u : 0u));
            synth.all_sound_off();
            synth.add_event_note_on (0, 0, external_switch, 100);
            synth.add_event_note_on (0, 0, note, 100);
            synth.process (out, 64);
            assert (synth.active_voice_count() == 1);
          }
      }
}

void
test_sample_overrides()
{
  // #56, F16: each sample opcode replaces both file and generator selection.
  printf ("test sample overrides\n");
  write_sample (vector<float> (1024, 0.25), 44100);

  auto render = [] (const string& sfz) {
    write_sfz (sfz);
    Synth synth;
    synth.set_sample_rate (44100);
    const bool loaded = synth.load ("testsynth.sfz");
    assert (loaded);
    vector<float> left (256), right (256);
    float *out[] = { left.data(), right.data() };
    synth.add_event_note_on (0, 0, 60, 100);
    synth.process (out, left.size());
    left.insert (left.end(), right.begin(), right.end());
    return left;
  };

  for (const string target : { "testsynth.wav", "*sine", "*silence" })
    {
      const auto reference = render ("<region>sample=" + target);
      assert ((peak (reference) > 0) == (target != "*silence"));
      for (const string source : { "testsynth.wav", "*sine", "*silence" })
        for (const string scope : { "<global>", "<master>", "<group>", "<region>" })
          {
            // Also cover repeated assignments within one region.
            const string region = scope == "<region>" ? " " : " <region>";
            const auto actual = render (scope + "sample=" + source + region + "sample=" + target);
            assert (actual == reference);
          }
    }

  for (const string source : { "testsynth.wav", "*sine", "*silence" })
    for (const string target : { "missing-sample-f16.wav", "*unsupported" })
      {
        write_sfz ("<global>sample=" + source + " <region>sample=" + target);
        Synth synth;
        string messages;
        synth.set_log_function ([&] (LiquidSFZ::Log, const char *message) { messages += message; });
        const bool loaded = synth.load ("testsynth.sfz");
        if (target == "*unsupported")
          {
            assert (!loaded); // Unsupported generators leave no valid selection.
            assert (messages.find ("unsupported generator *unsupported") != string::npos);
          }
        else
          {
            assert (loaded); // Missing files retain the existing warning behavior.
            assert (messages.find ("missing sample:") != string::npos);
            assert (messages.find (target) != string::npos);
          }
        float left[64], right[64];
        float *out[] = { left, right };
        synth.add_event_note_on (0, 0, 60, 100);
        synth.process (out, 64);
        assert (synth.active_voice_count() == 0);
      }
}

namespace LiquidSFZInternal
{
struct SampleCacheTestAccess
{
  static std::unique_lock<std::mutex>
  lock (SampleCache& cache)
  {
    return std::unique_lock<std::mutex> (cache.mutex_);
  }
};
}

void
test_sample_eviction_request()
{
  printf ("test playback starting after eviction selection\n");
  using namespace LiquidSFZInternal;
  write_sample (vector<float> (441000, 0.25), 44100);
  SampleCache cache;
  // Direct sample operations share the file pool with background cleanup.
  // Hold the cache lock until after the sample and its handles are destroyed.
  auto cache_lock = SampleCacheTestAccess::lock (cache);
  // Keep this sample outside the cache's background list so the ordering below
  // is deterministic. This thread performs all loader/eviction operations.
  Sample sample (&cache);
  auto preload = sample.add_preload (1, 0);
  const bool loaded = sample.preload ("testsynth.wav");
  assert (loaded);
  assert (!sample.playing()); // The cache could select it for eviction here.

  Sample::PlayHandle reader;
  reader.start_playback (&sample, true);
  const float missing = reader.get (220500); // Publish a request beyond preload.
  assert (missing == 0);
  sample.unload(); // Act on the stale eviction selection after playback starts.
  sample.load();

  // A fresh handle avoids the failed-lookup backoff in the requesting handle.
  Sample::PlayHandle check;
  check.start_playback (&sample, true);
  assert (check.get (220500) == 0.25f);
  const size_t playing_size = cache.cache_size();
  check.end_playback();
  reader.end_playback();

  // Once idle, eviction must still discard the streamed data.
  sample.unload();
  sample.free_unused_data();
  assert (cache.cache_size() < playing_size);
  check.start_playback (&sample, true);
  assert (check.get (0) == 0.25f);
  assert (check.get (220500) == 0);
}

void
test_cached_sample_preload()
{
  printf ("test enlarged preload on cache hits\n");
  write_sample (vector<float> (441000, 0.25), 44100);

  // Inspect the new prefix before starting playback, so the background loader
  // cannot hide a missing synchronous preload.
  for (bool use_offset : { false, true })
    {
      using namespace LiquidSFZInternal;
      SampleCache cache;
      auto first = cache.load ("testsynth.wav", 1, 0);
      assert (first.sample);
      auto second = cache.load ("testsynth.wav", use_offset ? 1 : 5000, use_offset ? 220500 : 0);
      assert (second.sample == first.sample);
      assert (cache.cache_size() >= 220501 * sizeof (float));
      Sample::PlayHandle handle;
      handle.start_playback (second.sample.get(), true);
      assert (handle.get (220500) == 0.25f);
      assert (cache.cache_miss_count() == 0);
    }

  auto render_note = [] (Synth& synth, int key) {
    vector<float> left (64), right (64);
    float *out[] = { left.data(), right.data() };
    synth.add_event_note_on (0, 0, key, 127);
    synth.process (out, left.size());
    assert (synth.active_voice_count() > 0);
    assert (peak (left) > 0);
    assert (synth.cache_miss_count() == 0);
  };
  const string early = "<region>sample=testsynth.wav key=60 offset=0 ";
  const string late = "<region>sample=testsynth.wav key=61 offset=220500 ";
  for (bool reverse : { false, true })
    {
      write_sfz (reverse ? late + early : early + late);
      Synth synth;
      synth.set_sample_rate (44100);
      synth.set_preload_time (1);
      synth.set_live_mode (true);
      const bool loaded = synth.load ("testsynth.sfz");
      assert (loaded);
      render_note (synth, 61);
      render_note (synth, 60);
    }

  // A second synth can extend a sample that the first is already playing.
  {
    Synth first, second;
    for (Synth *synth : { &first, &second })
      {
        synth->set_sample_rate (44100);
        synth->set_preload_time (1);
        synth->set_live_mode (true);
      }
    write_sfz (early);
    bool loaded = first.load ("testsynth.sfz");
    assert (loaded);
    render_note (first, 60);
    write_sfz (late);
    loaded = second.load ("testsynth.sfz");
    assert (loaded);
    render_note (second, 61);
    float left[64], right[64];
    float *out[] = { left, right };
    first.process (out, 64);
    assert (first.active_voice_count() == 1);
    assert (left[63] != 0);
    assert (first.cache_miss_count() == 0);
  }
}

void
test_pitch_caps()
{
  printf ("test pitch caps (#56, I03)\n");
  for (int channels : { 1, 2 })
    {
      write_sample (vector<float> (4096 * channels, 0.25f), 48000, channels);
      for (const string sample : { "testsynth.wav", "*sine" })
        for (int quality : { 1, 2, 3 })
          for (int block_size : { 1, 17, 64, 1024 })
            for (const string pitch : {
                   "transpose=2000", // infinite replay speed
                   "transpose=768",  // finite position outside the integer range
                   "lfo1_freq=1 lfo1_pitch=1e30 lfo1_phase=0.25",
                   "transpose=2000 lfo1_freq=1 lfo1_pitch=1e30 lfo1_phase=0.25",
                   "transpose=-2000 lfo1_freq=1 lfo1_pitch=1e30 lfo1_phase=0.25", // 0 * inf
                   "tune_oncc1=1e100", // zero CC times an infinite amount
                   "tune_oncc1=1000000" })
              {
                write_sfz ("<region>sample=" + sample + " loop_mode=no_loop " + pitch);
                Synth synth;
                synth.set_sample_rate (48000);
                synth.set_sample_quality (quality);
                synth.set_live_mode (false);
                const bool loaded = synth.load ("testsynth.sfz");
                assert (loaded);
                float left[1024], right[1024];
                float *out[] = { left, right };
                auto render = [&] {
                  synth.process (out, block_size);
                  for (int i = 0; i < block_size; i++)
                    assert (std::isfinite (left[i]) && std::isfinite (right[i]));
                };
                synth.add_event_note_on (0, 0, 60, 127);
                render();
                if (pitch == "tune_oncc1=1000000")
                  {
                    assert (synth.active_voice_count() == 1);
                    synth.add_event_cc (0, 0, 1, 127);
                    render();
                  }
                for (int i = 0; i < 8; i++)
                  render();
                if (sample == "*sine")
                  assert (synth.active_voice_count() == 1);
              }
    }
}

void
test_offset_bounds()
{
  printf ("test offset bounds (#56, I03)\n");
  for (int channels : { 1, 2 })
    {
      vector<float> samples (4096 * channels);
      for (size_t i = 0; i < samples.size(); i++)
        samples[i] = 0.2 + 0.1 * sin (double (i / channels) * 0.1);
      write_sample (samples, 44100, channels);
      for (int quality : { 1, 2, 3 })
        {
          auto render = [&] (const string& opcodes, bool silent = false, bool interpolation_tail = false) {
            write_sfz ("<control>set_cc1=127 <region>sample=testsynth.wav "
                       "loop_mode=loop_continuous loop_start=1000 loop_end=2000 " + opcodes);
            Synth synth;
            synth.set_sample_rate (44100);
            synth.set_sample_quality (quality);
            synth.set_live_mode (false);
            const bool loaded = synth.load ("testsynth.sfz");
            assert (loaded);
            vector<float> left (128), right (128);
            float *out[] = { left.data(), right.data() };
            synth.add_event_note_on (0, 0, 60, 127);
            synth.process (out, left.size());
            for (auto channel : { &left, &right })
              for (size_t i = 0; i < channel->size(); i++)
                {
                  const float value = (*channel)[i];
                  assert (std::isfinite (value));
                  if (silent && (!interpolation_tail || i >= 32))
                    assert (value == 0);
                }
            assert (silent ? synth.active_voice_count() == 0 : peak (left) > 0);
            return left;
          };
          assert (render ("offset_oncc1=-100") == render ("offset=0"));
          assert (render ("offset=-1 offset_random=-2") == render ("offset=0"));
          assert (render ("offset=1000 offset_oncc1=-100") == render ("offset=900"));
          assert (render ("offset=4294967295 offset_oncc1=-4294967296") == render ("offset=0"));
          render ("offset=4294967000", true);
          render ("offset=4294967296", true); // unsigned overflow must saturate, not wrap
          render ("offset=2147483648", true); // used to wrap to zero when upsampled
          render ("offset=4294967295 offset_oncc1=1", true);
          render ("offset=4294967000 offset_random=4294967295", true);
          // Starts just past the end may still read the interpolator's short tail.
          render ("offset=4096", true, true);
          render ("offset=1001 end=1000", true, true);
          render ("offset_oncc1=1e30", true);
          render ("offset_oncc1=1e100", true); // positive infinity after float conversion
          render ("offset_oncc2=1e100", true); // zero CC times infinity produces NaN
          assert (render ("offset_oncc1=-1e100") == render ("offset=0"));
          render ("offset=4095");
          render ("offset=1000 end=1000"); // end is inclusive
        }

      // Check the cache boundary directly, including its legitimate zero padding.
      using namespace LiquidSFZInternal;
      SampleCache cache;
      auto loaded = cache.load ("testsynth.wav", 500, 0);
      assert (bool (loaded.sample));
      for (sample_count_t pos : { -65LL * channels, -320LL * channels, 1LL << 40 })
        {
          Sample::PlayHandle handle;
          handle.start_playback (loaded.sample.get(), false);
          assert (handle.get (pos) == 0);
        }
      Sample::PlayHandle handle;
      handle.start_playback (loaded.sample.get(), false);
      assert (handle.get (-64 * channels) == 0);
      assert (handle.get (0) == samples[0]);
    }

  // Inspect preload before note-on so background streaming cannot hide a short prefix.
  write_sample (vector<float> (441000, 0.25f), 44100);
  for (const string opcodes : {
         "<region>offset=220500 offset_oncc1=-220500",
         "<curve>curve_index=7 v000=-1 v127=0 <region>offset_oncc1=-220500 offset_curvecc1=7",
         "<region>offset_oncc136=-220500",
         "<region>offset=4294967295 offset_random=1",
         "<region>offset=4294967295 offset_oncc1=1" })
    {
      write_sfz ("<global>sample=testsynth.wav " + opcodes);
      Synth synth;
      synth.set_preload_time (1);
      const bool loaded = synth.load ("testsynth.sfz");
      assert (loaded);
      assert (synth.cache_size() >= 220501 * sizeof (float));
    }
}

void
test_multi_wrap_loops()
{
  printf ("test multi-wrap loops (#56, I03)\n");
  constexpr int sample_rate = 44100;

  for (int channels : { 1, 2 })
    {
      vector<float> samples (256 * channels);
      for (int frame = 0; frame < 256; frame++)
        for (int channel = 0; channel < channels; channel++)
          samples[frame * channels + channel] = (frame + 1) * (channel ? -0.002f : 0.001f);
      write_sample (samples, sample_rate, channels);

      auto render = [&] (const string& opcodes, int quality, int frames, int loop_end = 5) {
        write_sfz ("<region>sample=testsynth.wav volume_cc7=0 pan_cc10=0 ampeg_attack=0 "
                   "loop_mode=loop_continuous loop_start=0 "
                   + string_printf ("loop_end=%d ", loop_end) + opcodes);
        Synth synth;
        synth.set_sample_rate (sample_rate);
        synth.set_sample_quality (quality);
        synth.set_live_mode (false);
        synth.set_gain (sqrt (2));
        assert (synth.load ("testsynth.sfz"));
        std::array<vector<float>, 2> output = { vector<float> (frames), vector<float> (frames) };
        float *out[] = { output[0].data(), output[1].data() };
        synth.add_event_note_on (0, 0, 60, 127);
        synth.process (out, frames);
        return output;
      };

      // 65536 modulo 6 is 4, so after the slower voice has completed its
      // first loop these voices must visit exactly the same loop positions.
      // This exercises multi-wrap position arithmetic and loop-edge
      // interpolation for both 1x and 2x upsampling paths.
      for (int quality : { 1, 2, 3 })
        {
          const auto multi_wrap = render ("pitch=19200", quality, 32); // speed 65536
          const auto reference = render ("pitch=2400", quality, 32);  // speed 4
          for (int channel = 0; channel < 2; channel++)
            for (int frame = 2; frame < 32; frame++)
              assert (fabs (multi_wrap[channel][frame] - reference[channel][frame]) < 1e-6);
        }

      // At speed 64, moving from position 0 crosses a six-frame loop ten
      // times. These cases cover exhausting the loop count before the jump is
      // wrapped, exactly on the final wrap, and during the following jump.
      struct FiniteLoopCase
      {
        int loop_count;
        vector<int> positions;
      };
      const vector<FiniteLoopCase> cases = {
        { 3,  { 0, 46, 110, 174, 238 } },
        { 10, { 0, 4, 68, 132, 196 } },
        { 12, { 0, 4, 56, 120, 184, 248 } }
      };
      for (const auto& finite_case : cases)
        {
          const auto output = render (string_printf ("pitch=7200 loop_count=%d", finite_case.loop_count),
                                      1, finite_case.positions.size());
          for (int channel = 0; channel < channels; channel++)
            for (size_t frame = 0; frame < finite_case.positions.size(); frame++)
              {
                const float expected = samples[finite_case.positions[frame] * channels + channel];
                assert (fabs (output[channel][frame] - expected) < 1e-6);
              }
        }

      // Also cover the ceiling-division boundary where distance from the loop
      // end is an exact multiple of the loop length: (64 - 4) / 5.
      const vector<int> exact_division_positions = { 0, 4, 3, 2, 1, 0 };
      const auto exact_division = render ("pitch=7200", 1, exact_division_positions.size(), 4);
      for (int channel = 0; channel < channels; channel++)
        for (size_t frame = 0; frame < exact_division_positions.size(); frame++)
          {
            const float expected = samples[exact_division_positions[frame] * channels + channel];
            assert (fabs (exact_division[channel][frame] - expected) < 1e-6);
          }
    }
}

static void
test_large_durations()
{
  for (const char *opcodes : { "delay=1e20 ampeg_attack=1e20", "ampeg_attack=1e20" })
    {
      {
        std::ofstream sfz ("testsynth.sfz");
        sfz << "<region> sample=*sine " << opcodes << '\n';
      }
      Synth synth;
      synth.set_sample_rate (48000);
      synth.set_live_mode (false);
      assert (synth.load ("testsynth.sfz"));
      synth.add_event_note_on (0, 0, 60, 127);
      std::array<float, 1024> left {}, right {};
      float *out[] = { left.data(), right.data() };
      synth.process (out, left.size());
      for (size_t i = 0; i < left.size(); i++)
        {
          assert (std::isfinite (left[i]) && std::isfinite (right[i]));
          if (opcodes[0] == 'd')
            assert (left[i] == 0 && right[i] == 0);
        }
    }
}

void
test_key_state_revision()
{
  printf ("test keyboard state revision\n");
  write_sfz ("<group> sw_lokey=24 sw_hikey=25 sw_default=24\n"
             "<region> sample=*sine key=60 sw_last=24\n"
             "<region> sample=*sine key=61 sw_last=25\n");
  Synth synth;
  auto revision = synth.key_state_revision();
  assert (synth.load ("testsynth.sfz"));
  assert (synth.key_state_revision() > revision);
  float left[64], right[64];
  float *outputs[] = { left, right };
  auto process = [&] (bool changed)
    {
      const auto before = synth.key_state_revision();
      synth.process (outputs, 64);
      if (changed)
        assert (synth.key_state_revision() > before);
      else
        assert (synth.key_state_revision() == before);
      // Reading a snapshot must not consume changes or modify the revision.
      const auto after = synth.key_state_revision();
      synth.key_states();
      synth.key_states();
      assert (synth.key_state_revision() == after);
    };
  auto note = [&] (int channel, int key, int velocity, bool changed)
    {
      const auto before = synth.key_state_revision();
      synth.add_event_note_on (0, channel, key, velocity);
      assert (synth.key_state_revision() == before); // Queuing is not processing.
      process (changed);
    };
  process (false);
  synth.add_event_cc (0, 0, 7, 100);
  synth.add_event_pitch_bend (0, 0, 9000);
  process (false);
  note (0, 60, 100, true);
  note (0, 60, 100, false); // Retrigger does not change the held-key state.
  note (1, 60, 100, true);
  const auto held = synth.key_states();
  revision = synth.key_state_revision();
  synth.add_event_note_off (0, 0, 60);
  assert (synth.key_state_revision() == revision);
  process (true);
  assert (synth.key_states() == held); // Conservative change across channels.
  note (1, 60, 0, true);
  note (1, 60, 0, false);
  assert (!(synth.key_states()[60] & Synth::KEY_PRESSED));
  note (0, 60, 100, true);
  note (1, 60, 0, false); // Releasing an unheld channel must not decrement the count.
  assert (synth.key_states()[60] & Synth::KEY_PRESSED);
  note (0, 60, 0, true);
  assert (!(synth.key_states()[60] & Synth::KEY_PRESSED));

  note (0, 24, 100, true);
  note (0, 25, 100, true);
  note (0, 24, 100, true); // Already held, but changes the selected switch.
  assert (synth.key_states()[24] & Synth::KEY_SWITCH_ACTIVE);
  note (0, 24, 100, false);
  for (int cc : { 120, 123 })
    {
      note (0, 60, 100, true);
      synth.add_event_cc (0, 0, cc, 0);
      process (true);
      assert (!(synth.key_states()[60] & Synth::KEY_PRESSED));
    }
  for (bool reset : { false, true })
    {
      note (0, 60, 100, true);
      revision = synth.key_state_revision();
      if (reset)
        synth.system_reset();
      else
        synth.all_sound_off();
      assert (synth.key_state_revision() > revision);
      assert (!(synth.key_states()[60] & Synth::KEY_PRESSED));
    }
  revision = synth.key_state_revision();
  assert (synth.load ("testsynth.sfz"));
  assert (synth.key_state_revision() > revision); // Reload must not reset it.
  revision = synth.key_state_revision();
  assert (!synth.load ("")); // Failed load unloads the previous instrument.
  assert (synth.key_state_revision() > revision);
  for (auto state : synth.key_states())
    assert (state == 0);
}

void
test_keyboard_state()
{
  printf ("test keyboard state\n");
  for (int octave_offset : { -1, 0, 1 })
    {
      const int shift = octave_offset * 12;
      write_sfz (string_printf (
        "<control> octave_offset=%d\n"
        "<group> sw_lokey=24 sw_hikey=26 sw_default=24\n"
        "<region> sample=*sine lokey=60 hikey=61 sw_lolast=24 sw_hilast=25\n"
        "<region> sample=*sine lokey=62 hikey=63 sw_last=26\n"
        "<group> sw_lokey=36 sw_hikey=37 sw_default=36\n"
        "<region> sample=*sine key=80 sw_last=36\n"
        "<group>\n<region> sample=*sine key=70\n", octave_offset));
      Synth synth;
      assert (synth.load ("testsynth.sfz"));
      float left[64], right[64];
      float *outputs[] = { left, right };
      auto process = [&] { synth.process (outputs, 64); };
      auto state = [&] (int key) { return synth.key_states()[key + shift]; };
      auto note = [&] (int channel, int key, int velocity)
        {
          synth.add_event_note_on (0, channel, key + shift, velocity);
          process();
        };
      assert (state (24) == Synth::KEY_SWITCH_ACTIVE);
      assert (state (25) == Synth::KEY_SWITCH_ACTIVE);
      assert (state (60) == Synth::KEY_REGION_ACTIVE);
      assert (state (61) == Synth::KEY_REGION_ACTIVE);
      assert (state (62) == 0 && state (70) == 0);
      note (0, 26, 100);
      assert (state (26) == (Synth::KEY_SWITCH_ACTIVE | Synth::KEY_PRESSED));
      assert (state (24) == 0 && state (25) == 0 && state (60) == 0);
      assert (state (62) == Synth::KEY_REGION_ACTIVE && state (63) == Synth::KEY_REGION_ACTIVE);
      assert (state (80) == Synth::KEY_REGION_ACTIVE); // Independent switch group.
      note (0, 26, 0);
      assert (state (26) == Synth::KEY_SWITCH_ACTIVE); // Selection survives release.
      note (1, 25, 100);
      assert (state (60) == Synth::KEY_REGION_ACTIVE && state (62) == 0);
      note (1, 25, 0);
      note (0, 60, 100);
      note (1, 60, 100);
      synth.add_event_note_off (0, 0, 60 + shift);
      process();
      assert (state (60) & Synth::KEY_PRESSED);
      synth.add_event_cc (0, 1, 64, 127);
      note (1, 60, 0);
      assert (!(state (60) & Synth::KEY_PRESSED)); // Sustain is not a held key.
      note (0, 61, 100);
      synth.add_event_cc (0, 0, 123, 0);
      process();
      assert (!(state (61) & Synth::KEY_PRESSED));
      note (0, 60, 100);
      synth.all_sound_off();
      assert (state (60) == Synth::KEY_REGION_ACTIVE);

      write_sfz ("<region> sample=*sine key=60 sw_lokey=24 sw_hikey=25 sw_last=24");
      assert (synth.load ("testsynth.sfz"));
      for (auto flags : synth.key_states())
        assert (flags == 0); // No sw_default: no selected region after reload.
    }
}

int
main()
{
  test_key_state_revision();
  test_keyboard_state();
  test_large_durations();
  test_pitch_caps();
  test_offset_bounds();
  test_multi_wrap_loops();
  test_sample_eviction_request();
  test_key_metadata_and_switches();
  test_cached_sample_preload();
  test_sample_overrides();
  test_filesystem_load_errors();
  test_velocity_zero_note_off();
  test_simple();
  test_interp_time_align();
  test_tiny_loop();
  test_wav_loop();
  test_pitch();
  test_width();
  test_end();
  test_filter();
  test_fil_type();
  test_unsupported_peq_filter();
  test_grow_voices_with_lfo();
  test_lfo_phase_wrapping();
  test_repro_lfo_alloc();
  test_unload_playing_sample();
  test_bank_definitions();

  unlink ("testsynth.sfz");
  unlink ("testsynth.wav");
}
