// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "sfpool.hh"

#include <array>
#include <cstdio>
#include <string>

int
main()
{
  // A committed fixture makes this a decoder test, independent of encoder support.
  const std::string filename = std::string (TEST_SRCDIR) + "/fixtures/stereo.flac";
  LiquidSFZInternal::SFPool pool;
  SF_INFO info = {};
  auto entry = pool.open (filename, &info);
  if (!entry || !entry->sndfile)
    {
      fprintf (stderr, "Cannot decode %s: %s (FLAC support is required)\n",
               filename.c_str(), sf_strerror (nullptr));
      return 1;
    }

  constexpr std::array<int, 16> pcm = {
    0, 32767, -32768, 0, 8192, -16384, 24576, -8192,
    1, -1, 12345, -23456, -16384, 16384, 0, 0
  };
  constexpr int channels = 2;
  constexpr int frames = pcm.size() / channels;
  if (info.samplerate != 48000 || info.channels != channels || info.frames != frames
      || info.format != (SF_FORMAT_FLAC | SF_FORMAT_PCM_16))
    {
      fprintf (stderr, "Unexpected FLAC metadata: rate=%d channels=%d frames=%lld format=0x%x\n",
               info.samplerate, info.channels, static_cast<long long> (info.frames), info.format);
      return 1;
    }

  std::array<float, pcm.size()> decoded = {};
  if (entry->seek_read_frames (0, decoded.data(), frames) != frames)
    {
      fprintf (stderr, "Could not read all FLAC frames\n");
      return 1;
    }
  for (size_t i = 0; i < pcm.size(); i++)
    {
      // PCM16 normalization is exact in float, so no tolerance is needed.
      const float expected = pcm[i] / 32768.f;
      if (decoded[i] != expected)
        {
          fprintf (stderr, "FLAC sample %zu: got %.9g, expected %.9g\n", i, decoded[i], expected);
          return 1;
        }
    }
  puts ("FLAC decoding passed");
  return 0;
}
