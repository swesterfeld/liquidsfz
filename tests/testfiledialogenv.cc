// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
enum class ReadMode { NORMAL, OPEN_ERROR, READ_ERROR, INTERRUPTED };
ReadMode read_mode;
std::string proc_contents;
int injected_error;
bool injected;
int retries;

FILE *
proc_fopen (const char *path, const char *mode)
{
  assert (strcmp (path, "/proc/self/environ") == 0);
  assert (strcmp (mode, "re") == 0);
  if (read_mode == ReadMode::OPEN_ERROR)
    {
      errno = ENOENT;
      return nullptr;
    }
  FILE *file = tmpfile();
  assert (file);
  assert (fwrite (proc_contents.data(), 1, proc_contents.size(), file) == proc_contents.size());
  rewind (file);
  return file;
}

size_t
proc_fread (void *buffer, size_t size, size_t count, FILE *file)
{
  assert (size == 1);
  if (!injected && (read_mode == ReadMode::READ_ERROR || read_mode == ReadMode::INTERRUPTED))
    {
      // A stdio read may deliver bytes and set the error indicator together.
      const size_t result = fread (buffer, size, std::min (count, size_t (7)), file);
      injected = true;
      injected_error = read_mode == ReadMode::INTERRUPTED ? EINTR : EIO;
      errno = injected_error;
      return result;
    }
  return fread (buffer, size, count, file);
}

int
proc_ferror (FILE *file)
{
  return injected_error || ferror (file);
}

void
proc_clearerr (FILE *file)
{
  assert (injected_error == EINTR);
  injected_error = 0;
  ++retries;
  clearerr (file);
}
}

// Substitute only procfs I/O in this test translation unit. This exercises the
// production reader on every platform without requiring procfs or linker tricks.
#define fopen proc_fopen
#define fread proc_fread
#define ferror proc_ferror
#define clearerr proc_clearerr
#include "../lv2/filedialog.cc"
#undef fopen
#undef fread
#undef ferror
#undef clearerr

int
main()
{
  alarm (5);
  assert (setenv ("LIQUIDSFZ_DIALOG_TEST", "live", 1) == 0);
  assert (setenv ("ARDOUR_BUNDLED", "true", 1) == 0);
  assert (setenv ("PREBUNDLE_ENV", "LIQUIDSFZ_DIALOG_TEST=restored\n", 1) == 0);

  proc_contents = std::string ("LONG=") + std::string (9000, 'x') + '\0'
                + "LIQUIDSFZ_DIALOG_TEST=startup" + '\0' + "MULTILINE=a\nb=c";
  for (const auto mode : { ReadMode::NORMAL, ReadMode::OPEN_ERROR,
                           ReadMode::READ_ERROR, ReadMode::INTERRUPTED })
    {
      read_mode = mode;
      injected = false;
      injected_error = 0;
      retries = 0;
      const auto snapshot = environment_snapshot();
      const auto env = helper_environment (snapshot);
      const bool fallback = mode == ReadMode::OPEN_ERROR || mode == ReadMode::READ_ERROR;
      assert (strcmp (environment_value (snapshot, "LIQUIDSFZ_DIALOG_TEST"),
                      fallback ? "live" : "startup") == 0);
      if (fallback)
        {
          assert (!environment_value (snapshot, "LONG")); // Discard partial reads.
          assert (env.size() == 1 && env[0] == "LIQUIDSFZ_DIALOG_TEST=restored");
        }
      else
        {
          assert (snapshot.size() == 3 && env == snapshot);
          assert (snapshot[0] == "LONG=" + std::string (9000, 'x'));
          assert (snapshot[2] == "MULTILINE=a\nb=c");
        }
      assert (retries == (mode == ReadMode::INTERRUPTED ? 1 : 0));
    }

  // A successful empty read must not fall back to the live environment.
  read_mode = ReadMode::NORMAL;
  proc_contents.clear();
  assert (environment_snapshot().empty());
  alarm (0);
}
