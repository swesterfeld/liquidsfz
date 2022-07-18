#include <fftw3.h>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <vector>
#include <sndfile.h>

#include "upsample.hh"
#include "liquidsfz.hh"

using namespace LiquidSFZInternal;

using std::vector;
using std::string;

inline double
window_blackman (double x)
{
  if (fabs (x) > 1)
    return 0;
  return 0.42 + 0.5 * cos (M_PI * x) + 0.08 * cos (2.0 * M_PI * x);
}

inline double
window_blackman_harris_92 (double x)
{
  if (fabs (x) > 1)
    return 0;

  const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;

  return a0 + a1 * cos (M_PI * x) + a2 * cos (2.0 * M_PI * x) + a3 * cos (3.0 * M_PI * x);
}

void
fft (const uint n_values, float *r_values_in, float *ri_values_out)
{
  auto plan_fft = fftwf_plan_dft_r2c_1d (n_values, r_values_in, (fftwf_complex *) ri_values_out, FFTW_ESTIMATE | FFTW_PRESERVE_INPUT);

  fftwf_execute_dft_r2c (plan_fft, r_values_in, (fftwf_complex *) ri_values_out);

  // usually we should keep the plan, but for this simple test program, the fft
  // is only computed once, so we can destroy the plan here
  fftwf_destroy_plan (plan_fft);
}

double
db (double x)
{
  return 20 * log10 (std::max (x, 0.00000001));
}

int
main (int argc, char **argv)
{
  if (argc == 3 && string (argv[1]) == "synth")
    {
      //float freq = atof (argv[2]);
      int sample_quality = atoi (argv[2]);
      struct Result {
        float freq;
        double pass;
        double stop;
      };
      vector<Result> results;
      for (float freq = 50; freq < 22050; freq += 25)
        {
          static constexpr int RATE_FROM = 44100;
          static constexpr int UPSAMPLE = 2;
          static constexpr int RATE_TO = 48000 * UPSAMPLE;

          auto f = [freq] (int x) -> float { return sin (x * 2 * M_PI * freq / RATE_FROM); };

          SF_INFO sfinfo = {0,};
          sfinfo.samplerate = RATE_FROM;
          sfinfo.channels = 1;
          sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

          SNDFILE *sndfile = sf_open ("testupsample.wav", SFM_WRITE, &sfinfo);
          assert (sndfile);

          vector<float> samples;
          int n = 4096 * 4;
          double window_weight = 0;
          for (int i = 0; i < n; i++)
            {
              const double wsize_2 = n / 2;
              const double w = window_blackman_harris_92 ((i - wsize_2) / wsize_2);
              window_weight += w;
              samples.push_back (f (i) * w);
            }

          sf_count_t count = sf_writef_float (sndfile, &samples[0], samples.size());
          assert (count == sf_count_t (samples.size()));
          sf_close (sndfile);

          LiquidSFZ::Synth synth;
          synth.set_sample_rate (RATE_TO);
          synth.set_live_mode (false);
          synth.set_sample_quality (sample_quality);
          if (!synth.load ("testupsample.sfz"))
            {
              fprintf (stderr, "parse error: exiting\n");
              return 1;
            }
          synth.set_gain (sqrt (2));
          size_t out_size = n * 2 * UPSAMPLE;
          std::vector<float> out (out_size), out_right (out_size);
          float *outputs[2] = { out.data(), out_right.data() };
          synth.add_event_note_on (0, 0, 60, 127);
          synth.process (outputs, out_size);

          for (auto& x : out)
            x *= 2 / window_weight * RATE_FROM / RATE_TO;

          // zero pad
          vector<float> padded (out);
          padded.resize (padded.size() * 4);
          vector<float> out_fft (padded.size());
          fft (padded.size(), &padded[0], &out_fft[0]);
          double pass = 0;
          double stop = 0;
          for (size_t i = 0; i < padded.size(); i += 2)
            {
              auto re = out_fft[i];
              auto im = out_fft[i + 1];
              auto amp = sqrt (re * re + im * im);
              auto norm_freq = RATE_TO / 2.0 * i / padded.size();
              //printf ("%f %f\n", norm_freq, db (amp));
              if (fabs (norm_freq - freq) < 20)
                pass = std::max (pass, amp);
              else
                stop = std::max (stop, amp);
            }
          results.push_back ({ freq, db (pass), db (stop) });
        }
      for (auto it = results.begin(); it != results.end(); it++)
        {
          printf ("%f %.17g\n", it->freq, it->pass);
        }
      for (auto it = results.rbegin(); it != results.rend(); it++)
        {
          printf ("%f %.17g\n", it->freq, it->stop);
        }
    }
  if (argc == 3 && string (argv[1]) == "synth-saw")
    {
      int sample_quality = atoi (argv[2]);
      static constexpr int RATE_FROM = 44100;
      static constexpr int RATE_TO = 48000;

      SF_INFO sfinfo = {0,};
      sfinfo.samplerate = RATE_FROM;
      sfinfo.channels = 1;
      sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

      SNDFILE *sndfile = sf_open ("testupsample.wav", SFM_WRITE, &sfinfo);
      assert (sndfile);

      vector<float> samples;
      int n = 4096 * 4;
      double window_weight = 0;
      for (int i = 0; i < n; i++)
        {
          const double wsize_2 = n / 2;
          const double w = window_blackman_harris_92 ((i - wsize_2) / wsize_2);
          window_weight += w;
          samples.push_back (((i % 100) - 49.5) / 49.5 * w);
        }

      sf_count_t count = sf_writef_float (sndfile, &samples[0], samples.size());
      assert (count == sf_count_t (samples.size()));
      sf_close (sndfile);

      LiquidSFZ::Synth synth;
      synth.set_sample_rate (RATE_TO);
      synth.set_live_mode (false);
      synth.set_sample_quality (sample_quality);
      if (!synth.load ("testupsample.sfz"))
        {
          fprintf (stderr, "parse error: exiting\n");
          return 1;
        }
      synth.set_gain (sqrt (2));
      size_t out_size = n * 2;
      std::vector<float> out (out_size), out_right (out_size);
      float *outputs[2] = { out.data(), out_right.data() };
      synth.add_event_note_on (0, 0, 60, 127);
      synth.process (outputs, out_size);

      for (auto& x : out)
        {
          x *= 2 / window_weight * RATE_FROM / RATE_TO;
        }
      // zero pad
      vector<float> padded (out);
      padded.resize (padded.size() * 4);
      vector<float> out_fft (padded.size());
      fft (padded.size(), &padded[0], &out_fft[0]);
      for (size_t i = 0; i < padded.size(); i += 2)
        {
          auto re = out_fft[i];
          auto im = out_fft[i + 1];
          auto amp = sqrt (re * re + im * im);
          auto norm_freq = RATE_TO / 2.0 * i / padded.size();
          printf ("%f %f\n", norm_freq, db (amp));
        }
    }
  if (argc == 2 && string (argv[1]) == "upsample")
    {
      for (float freq = 50; freq < 22050; freq += 25)
        {
          auto f = [freq] (int x) -> float { return sin (x * 2 * M_PI * freq / 44100); };

          const int N = 64; // upsample can access samples with negative index
          int n = 4096 * 4;
          vector<float> in (n + 2 * N);
          for (int i = 0; i < n + 2 * N; i++)
            in[i] = f (i - N);

          vector<float> out (n * 2);
          for (int i = 0; i < n; i++)
            upsample<1> (&in[i + N], &out[i * 2]);

          double window_weight = 0;
          for (int i = 0; i < n * 2; i++)
            {
              const double wsize_2 = n;
              const double w = window_blackman ((i - wsize_2) / wsize_2);
              out[i] *= w;
              window_weight += w;
            }
          for (auto& x : out)
            x *= 2 / window_weight;

          // zero pad
          vector<float> padded (out);
          padded.resize (padded.size() * 4);
          vector<float> out_fft (padded.size());
          fft (padded.size(), &padded[0], &out_fft[0]);
          double pass = 0;
          double stop = 0;
          for (size_t i = 0; i < padded.size(); i += 2)
            {
              auto re = out_fft[i];
              auto im = out_fft[i + 1];
              auto amp = sqrt (re * re + im * im);
              //printf ("%d %f\n", i, amp);
              if (i < padded.size() / 2)
                pass = std::max (pass, amp);
              else
                stop = std::max (stop, amp);
            }
          printf ("%f %f %f\n", freq, db (pass), db (stop));
        }
    }
}
