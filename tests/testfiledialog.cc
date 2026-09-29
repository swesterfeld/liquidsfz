// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "../lv2/filedialog.hh"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
using std::string;

static string
finish (FileDialog& dialog)
{
  string filename;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (dialog.is_open())
    {
      filename += dialog.get_filename();
      assert (std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for (1ms);
    }
  return filename;
}

int
main (int argc, char **argv)
{
  // The same executable is a controlled fake dialog helper: no desktop needed.
  if (argc > 1)
    {
      if (strcmp (argv[1], "environment") == 0)
        {
          const char *value = getenv ("LIQUIDSFZ_DIALOG_TEST");
          if (!value || strcmp (value, "original=environment") != 0 || getenv ("ARDOUR_BUNDLED"))
            return 1;
          const char result[] = "/tmp/environment.sfz\n";
          return write (STDOUT_FILENO, result, sizeof (result) - 1) == sizeof (result) - 1 ? 0 : 1;
        }
      if (strcmp (argv[1], "cancel") == 0)
        return 1;

      const int gate = atoi (argv[2]);
      const int ready = atoi (argv[3]);
      assert (write (STDOUT_FILENO, "/tmp/partial", 12) == 12);
      const bool close_stdout = strcmp (argv[1], "close-stdout") == 0;
      if (close_stdout)
        close (STDOUT_FILENO);
      assert (write (ready, "!", 1) == 1);
      char byte;
      assert (read (gate, &byte, 1) == 1);
      if (!close_stdout)
        assert (write (STDOUT_FILENO, " name.sfz\n", 10) == 10);
      return 0;
    }

  // A regression that blocks in read/waitpid must fail instead of hanging CI.
  alarm (15);
  const string helper = std::filesystem::absolute (argv[0]).string();
  for (const char *mode : { "partial", "close-stdout" })
    {
      int gate[2], ready[2];
      assert (pipe (gate) == 0);
      assert (pipe (ready) == 0);
      int reused_fd = -1;
      {
        FileDialog dialog (std::vector<string> { helper, mode, std::to_string (gate[0]), std::to_string (ready[1]) });
        assert (dialog.is_open());
        char byte;
        assert (read (ready[0], &byte, 1) == 1);

        // The child cannot finish until these calls return and we release it.
        // Check both partial output and EOF with a child that is still alive.
        assert (dialog.get_filename().empty());
        assert (dialog.get_filename().empty());
        assert (dialog.is_open());
        assert (write (gate[1], "!", 1) == 1);
        const string expected = strcmp (mode, "partial") == 0 ? "/tmp/partial name.sfz" : "/tmp/partial";
        assert (finish (dialog) == expected);
        assert (dialog.get_filename().empty());

        // Destruction must not close a descriptor reused after pipe EOF.
        reused_fd = open ("/dev/null", O_RDONLY);
        assert (reused_fd >= 0);
      }
      assert (fcntl (reused_fd, F_GETFD) >= 0);
      close (reused_fd);
      close (gate[0]);
      close (gate[1]);
      close (ready[0]);
      close (ready[1]);
    }

  {
    FileDialog dialog (std::vector<string> { helper, "cancel" });
    assert (finish (dialog).empty());
  }
  {
    FileDialog dialog (std::vector<string> { "/does/not/exist/liquidsfz-dialog-helper" });
    assert (!dialog.is_open());
    assert (dialog.get_filename().empty());
  }

  // Preserve the host environment while passing Ardour's saved copy to the child.
  const char *names[] = { "ARDOUR_BUNDLED", "PREBUNDLE_ENV", "LIQUIDSFZ_DIALOG_TEST" };
  string saved[3];
  bool present[3];
  for (int i = 0; i < 3; i++)
    {
      const char *value = getenv (names[i]);
      present[i] = value != nullptr;
      if (value)
        saved[i] = value;
    }
  setenv (names[0], "true", 1);
  setenv (names[1], "invalid\n=ignored\nLIQUIDSFZ_DIALOG_TEST=old\nLIQUIDSFZ_DIALOG_TEST=original=environment\n", 1);
  setenv (names[2], "bundled", 1);
  {
    FileDialog dialog (std::vector<string> { helper, "environment" });
    assert (finish (dialog) == "/tmp/environment.sfz");
    assert (strcmp (getenv (names[0]), "true") == 0);
    assert (strcmp (getenv (names[2]), "bundled") == 0);
  }
  for (int i = 0; i < 3; i++)
    if (present[i])
      setenv (names[i], saved[i].c_str(), 1);
    else
      unsetenv (names[i]);

  // Closing the UI with a live helper must kill and reap that child.
  {
    FileDialog dialog (std::vector<string> { "/bin/sleep", "30" });
    assert (dialog.is_open());
  }
  assert (waitpid (-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
  alarm (0);
}
