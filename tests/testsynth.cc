// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <cmath>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <unistd.h>

#include <sndfile.h>
#include <vector>
#include <algorithm>

#include "liquidsfz.hh"
#include "log.hh"
#include "config.h"

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

      vector<float> fft_values (padded_signal.size() + 2);
      fft (padded_signal.size(), padded_signal.data(), fft_values.data());

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

      double db_diff = db (peak (out_left));
      if (cmp)
        {
          printf (" - %s: expected db %.2f, got db %.2f\n", s.c_str(), expect, db_diff);
          assert (fabs (expect - db_diff) < 1e-3);
        }
      return db_diff;
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
      assert (fabs (l1000 - xl1000) < 0.01);
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

int
main (int argc, char **argv)
{
  test_simple();
  test_interp_time_align();
  test_tiny_loop();
  test_wav_loop();
  test_pitch();
  test_width();
  test_end();
  test_filter();

  unlink ("testsynth.sfz");
  unlink ("testsynth.wav");
}
