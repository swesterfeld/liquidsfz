// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "hydrogenimport.hh"

using namespace LiquidSFZInternal;

using std::string;

int
main (int argc, char **argv)
{
  Synth synth; // dummy for testing
  HydrogenImport himport (&synth);
  string out;
  if (!himport.detect (argv[1]))
    {
      printf ("This is not a hydrogen file\n");
      return 1;
    }
  himport.parse (argv[1], out);
  printf ("/////////////////////////////////////////////////////////////////////////////////////\n");
  printf ("%s", out.c_str());
  printf ("/////////////////////////////////////////////////////////////////////////////////////\n");
}
