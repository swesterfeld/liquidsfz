// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "envelope.hh"
#include <string.h>

using namespace LiquidSFZInternal;

int
main (int argc, char **argv)
{
  Envelope envelope;

  envelope.set_delay (atof (argv[1]));
  envelope.set_attack (atof (argv[2]));
  envelope.set_hold (atof (argv[3]));
  envelope.set_decay (atof (argv[4]));
  envelope.set_sustain (atof (argv[5]));
  envelope.set_release (atof (argv[6]));
  envelope.start (Region(), 48000);
  if (strcmp (argv[7], "lin") == 0)
    envelope.set_shape (Envelope::Shape::LINEAR);
  else if (strcmp (argv[7], "exp") == 0)
    envelope.set_shape (Envelope::Shape::EXPONENTIAL);
  else
    assert (false);

  for (int i = 0; i < 24000; i++)
    printf ("%f\n", envelope.get_next());

  envelope.stop (OffMode::NORMAL);

  for (int i = 0; i < 24000; i++)
    printf ("%f\n", envelope.get_next());
}
