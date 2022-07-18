// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "synth.hh"
#include "liquidsfz.hh"
#include "midnam.hh"

using namespace LiquidSFZ;

using LiquidSFZInternal::gen_midnam;

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: testliquid <sfz_filename>\n");
      return 1;
    }
  Synth synth;
  synth.set_sample_rate (48000);
  if (!synth.load (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }
  printf ("%s\n", gen_midnam (synth, "model").c_str());
}
