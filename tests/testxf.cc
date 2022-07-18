// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "voice.hh"

using namespace LiquidSFZInternal;

int
main (int argc, char **argv)
{
  Limits no_limits;
  Voice voice (nullptr, no_limits);

  int lo = atoi (argv[1]);
  int hi = atoi (argv[2]);

  for (int value = lo - 2; value <= hi + 2; value++)
    {
      printf ("%d %f %f %f %f\n", value,
              voice.xfin_gain (value, lo, hi, XFCurve::POWER), voice.xfout_gain (value, lo, hi, XFCurve::POWER),
              voice.xfin_gain (value, lo, hi, XFCurve::GAIN), voice.xfout_gain (value, lo, hi, XFCurve::GAIN));
    }
}
