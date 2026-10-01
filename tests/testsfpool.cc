// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "sfpool.hh"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using LiquidSFZInternal::SFPool;
using std::string;
namespace fs = std::filesystem;

#if defined(__linux__) && LIQUIDSFZ_64BIT
size_t
fd_count()
{
  size_t count = 0;
  for (const auto& entry : fs::directory_iterator ("/proc/self/fd"))
    {
      (void) entry;
      count++;
    }
  return count;
}

size_t
mapping_count (const string& filename)
{
  std::ifstream maps ("/proc/self/maps");
  assert (maps.is_open());
  size_t count = 0;
  string line;
  while (std::getline (maps, line))
    if (line.find (filename) != string::npos)
      count++;
  return count;
}
#endif

int
main()
{
#if defined(__linux__) && LIQUIDSFZ_64BIT
  // F08: exercise both mmap failure and decoder failure after a successful map.
  char dir_template[] = "/tmp/liquidsfz-sfpool-XXXXXX";
  char *dir = mkdtemp (dir_template);
  assert (dir);
  const string empty = string (dir) + "/empty.wav";
  const string invalid = string (dir) + "/invalid.wav";
  const string valid = string (dir) + "/valid.wav";
  std::ofstream (empty).close();
  std::ofstream (invalid) << "This is not an audio file.\n";

  SF_INFO info = {};
  info.samplerate = 48000;
  info.channels = 1;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  SNDFILE *file = sf_open (valid.c_str(), SFM_WRITE, &info);
  assert (file);
  const float samples[] = { 0.25f, -0.5f, 0.75f, 0.f };
  const auto written = sf_writef_float (file, samples, 4);
  assert (written == 4);
  const int closed = sf_close (file);
  assert (closed == 0);

  bool no_leaks = true;
  for (const auto& filename : { empty, invalid, valid })
    {
      const size_t fds_before = fd_count();
      const size_t maps_before = mapping_count (filename);
      for (int i = 0; i < 8; i++)
        {
          // Destroy the pool as well as the handle so every attempt opens anew.
          SFPool pool;
          SF_INFO read_info = {};
          auto entry = pool.open (filename, &read_info);
          assert (entry);
          if (filename == valid)
            {
              assert (entry->sndfile);
              float decoded[4] = {};
              const auto read = entry->seek_read_frames (0, decoded, 4);
              assert (read == 4);
              for (int j = 0; j < 4; j++)
                assert (decoded[j] == samples[j]);
            }
          else
            assert (!entry->sndfile);
        }
      const size_t fds_after = fd_count();
      const size_t maps_after = mapping_count (filename);
      printf ("%s: fds %zu -> %zu, mappings %zu -> %zu\n",
              filename.c_str(), fds_before, fds_after, maps_before, maps_after);
      no_leaks &= fds_before == fds_after && maps_before == maps_after;
    }
  fs::remove_all (dir);
  return no_leaks ? 0 : 1;
#else
  // This resource-counting regression requires the mmap backend and Linux procfs.
  // Automake treats exit code 77 as SKIP rather than a test failure.
  return 77;
#endif
}
