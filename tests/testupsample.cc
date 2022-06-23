#include <fftw3.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "upsample.hh"

using namespace LiquidSFZInternal;

using std::vector;

inline double
window_blackman (double x)
{
  if (fabs (x) > 1)
    return 0;
  return 0.42 + 0.5 * cos (M_PI * x) + 0.08 * cos (2.0 * M_PI * x);
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
  //float freq = atof (argv[1]);
  for (float freq = 50; freq < 22050; freq += 25)
    {
      auto f = [freq] (int x) { return sin (x * 2 * M_PI * freq / 44100); };
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
      for (int i = 0; i < padded.size(); i += 2)
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
