// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "../lv2/filedialog.hh"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
using std::string;

static bool
proc_environment_readable()
{
  FILE *file = fopen ("/proc/self/environ", "re");
  if (!file)
    return false;
  char buffer[4096];
  while (fread (buffer, 1, sizeof (buffer), file) > 0)
    {}
  const bool readable = !ferror (file);
  fclose (file);
  return readable;
}

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
      if (strcmp (argv[1], "snapshot") == 0)
        {
          alarm (5);
          const bool use_proc = proc_environment_readable();
          const bool bundled = getenv ("ARDOUR_BUNDLED") != nullptr;
          // procfs supplies startup values; without it, the fallback supplies
          // current values. Ardour restoration must work with either source.
          setenv ("LIQUIDSFZ_DIALOG_TEST", "changed", 1);
          setenv ("PREBUNDLE_ENV", "invalid\n=ignored\nLIQUIDSFZ_DIALOG_TEST=old\nLIQUIDSFZ_DIALOG_TEST=restored=environment\n", 1);
          setenv ("KDE_FULL_SESSION", "false", 1);
          const string expected = use_proc ? "original=environment"
                                          : bundled ? "restored=environment" : "changed";
          FileDialog dialog (std::vector<string> { argv[0], "environment", expected });
          assert (finish (dialog) == "/tmp/environment.sfz");
          assert (strcmp (getenv ("LIQUIDSFZ_DIALOG_TEST"), "changed") == 0);
          assert (strcmp (getenv ("KDE_FULL_SESSION"), "false") == 0);
          return 0;
        }
      if (strcmp (argv[1], "environment") == 0)
        {
          const char *value = getenv ("LIQUIDSFZ_DIALOG_TEST");
          if (argc != 3 || !value || strcmp (value, argv[2]) != 0 || getenv ("ARDOUR_BUNDLED"))
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

  // Install each test environment at exec time so procfs exposes it.
  for (bool bundled : { false, true })
    {
      std::vector<string> env = { "KDE_FULL_SESSION=true",
                                 "LIQUIDSFZ_DIALOG_TEST=original=environment" };
      if (bundled)
        {
          env.push_back ("ARDOUR_BUNDLED=true");
          env.push_back ("PREBUNDLE_ENV=invalid\n=ignored\nLIQUIDSFZ_DIALOG_TEST=old\nLIQUIDSFZ_DIALOG_TEST=original=environment\n");
        }
      std::vector<char *> envp;
      for (auto& entry : env)
        envp.push_back (entry.data());
      envp.push_back (nullptr);
      string mode = "snapshot";
      char *args[] = { const_cast<char *> (helper.c_str()), mode.data(), nullptr };
      pid_t child;
      assert (posix_spawn (&child, helper.c_str(), nullptr, nullptr, args, envp.data()) == 0);
      int status;
      assert (waitpid (child, &status, 0) == child);
      assert (WIFEXITED (status) && WEXITSTATUS (status) == 0);
    }

  // Closing the UI with a live helper must kill and reap that child.
  {
    FileDialog dialog (std::vector<string> { "/bin/sleep", "30" });
    assert (dialog.is_open());
  }
  assert (waitpid (-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
  alarm (0);
}
