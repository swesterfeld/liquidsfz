// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "synth.hh"
#include "liquidsfz.hh"
#include "argparser.hh"

using namespace LiquidSFZ;

using LiquidSFZInternal::ArgParser;

using std::vector;
using std::string;

int
main (int argc, char **argv)
{
  ArgParser ap (argc, argv);

  int sample_rate = 48000;
  int quality = -1;
  ap.parse_opt ("--rate", sample_rate);
  ap.parse_opt ("--quality", quality);

  vector<string> args;
  if (!ap.parse_args (1, args))
    {
      fprintf (stderr, "usage: testliquid <sfz_filename>\n");
      return 1;
    }
  Synth synth;
  if (quality > 0)
    synth.set_sample_quality (quality);
  synth.set_sample_rate (sample_rate);
  synth.set_live_mode (false);
  if (!synth.load (args[0]))
    {
      fprintf (stderr, "parse error: exiting\n");
      return 1;
    }

  std::vector<float> out_left (1024), out_right (1024);

  float *outputs[2] = { out_left.data(), out_right.data() };
  synth.add_event_note_on (0, 0, 60, 127);
  std::vector<float> interleaved;
  float left_peak = 0;
  float right_peak = 0;
  for (int pos = 0; pos < 100; pos++)
    {
      if (pos == 50)
        synth.add_event_note_off (0, 0, 60);

      synth.process (outputs, 1024);

      for (int i = 0; i < 1024; i++)
        {
          //printf ("%d %f %f\n", i, out_left[i], out_right[i]);
          interleaved.push_back (out_left[i]);
          interleaved.push_back (out_right[i]);
          left_peak = std::max (std::abs (out_left[i]), left_peak);
          right_peak = std::max (std::abs (out_right[i]), right_peak);
        }
    }

  printf ("left_peak %f\n", left_peak);
  printf ("right_peak %f\n", right_peak);

  SF_INFO sfinfo = {0, };
  sfinfo.samplerate = 48000;
  sfinfo.format = SF_FORMAT_PCM_24 | SF_FORMAT_WAV;
  sfinfo.channels = 2;

  SNDFILE *sndfile = sf_open ("testliquid.wav", SFM_WRITE, &sfinfo);

  sf_count_t frames = interleaved.size() / 2;
  sf_writef_float (sndfile, &interleaved[0], frames);
  printf ("%zd\n", frames);

  sf_close (sndfile);
}
