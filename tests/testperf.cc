// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "liquidsfz.hh"
#include "utils.hh"

#include <vector>

using namespace LiquidSFZ;
using namespace LiquidSFZInternal;

int
main (int argc, char **argv)
{
  size_t block_size = 1024;
  int note = 60;
  if (argc >= 5)
    note = atoi (argv[4]);
  if (argc >= 4)
    block_size = atoi (argv[3]);
  if (argc < 2 || argc > 5)
    {
      fprintf (stderr, "usage: testperf <sfz_filename> [ <quality> ] [ <block_size> ] [ <note> ]\n");
      return 1;
    }
  Synth synth;
  synth.set_sample_rate (48000);
  synth.set_live_mode (false);

  if (argc >= 3)
    synth.set_sample_quality (atoi(argv[2]));

  if (!synth.load (argv[1]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }

  std::vector<float> out_left (block_size), out_right (block_size);

  float *outputs[2] = { out_left.data(), out_right.data() };
  synth.add_event_note_on (0, 0, note, 127);

  /* since we are in non-live mode, we wait for samples to be
   * loaded by processing lots of blocks before we start measuring
   */
  uint n_warmup_blocks = 2000000 / block_size;
  for (uint pos = 0; pos < n_warmup_blocks; pos++)
    synth.process (outputs, block_size);

  const double time_start = get_time();
  uint samples = 0;
  uint n_blocks = 20000000 / block_size;
  for (uint pos = 0; pos < n_blocks; pos++)
    {
      synth.process (outputs, block_size);
      samples += block_size;
    }
  const double time_total = get_time() - time_start;
  printf ("time %f, samples %d, ns/sample %f bogo_voices %f\n", time_total, samples, time_total * 1e9 / samples, (samples / 48000.) / time_total);
}
