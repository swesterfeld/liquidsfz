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
  if (argc == 2 && string (argv[1]) == "synth")
    {
      //float freq = atof (argv[2]);
      for (float freq = 50; freq < 22050; freq += 25)
        {
          auto f = [freq] (int x) -> float { return sin (x * 2 * M_PI * freq / 44100); };

          SF_INFO sfinfo = {0,};
          sfinfo.samplerate = 44100;
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
          synth.set_sample_rate (96000);
          synth.set_live_mode (false);
          if (!synth.load ("testupsample.sfz"))
            {
              fprintf (stderr, "parse error: exiting\n");
              return 1;
            }
          synth.set_gain (sqrt (2));
          size_t out_size = n * 4;
          std::vector<float> out (out_size), out_right (out_size);

          float *outputs[2] = { out.data(), out_right.data() };
          synth.add_event_note_on (0, 0, 60, 127);
          synth.process (outputs, out_size);

          for (auto& x : out)
            x *= 2 / window_weight * 44100 / 96000;

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
              auto norm_freq = 96000. / 2 * i / padded.size();
              //printf ("%f %f\n", norm_freq, db (amp));
              if (fabs (norm_freq - freq) < 20)
                pass = std::max (pass, amp);
              else
                stop = std::max (stop, amp);
            }
          printf ("%f %f %f\n", freq, db (pass), db (stop));
        }
    }
  if (argc == 2 && string (argv[1]) == "upsample")
    {
      for (float freq = 50; freq < 22050; freq += 25)
        {
          auto f = [freq] (int x) -> float { return sin (x * 2 * M_PI * freq / 44100); };

          int n = 4096 * 4;
          vector<float> out (n * 2);
          for (int i = 0; i < n; i++)
            upsample (f, &out[i * 2], i);

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
