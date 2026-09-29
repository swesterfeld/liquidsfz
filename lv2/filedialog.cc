// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "filedialog.hh"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

using std::string;
using std::vector;

bool
FileDialog::have_helpers()
{
  return fs::exists (KDIALOG) || fs::exists (YAD) || fs::exists (ZENITY);
}

bool
FileDialog::is_kde_full_session()
{
  char *env = getenv ("KDE_FULL_SESSION");
  return env && (strcmp (env, "true") == 0);
}

void
FileDialog::restore_ardour_environment()
{
  const char *bundled = getenv ("ARDOUR_BUNDLED");
  if (!bundled || strcmp (bundled, "true") != 0)
    return;

  const char *saved = getenv ("PREBUNDLE_ENV");
  if (!saved || !*saved)
    return;

  // Make a copy before clearenv() invalidates the getenv() pointer.
  string original (saved);

  clearenv();

  // PREBUNDLE_ENV is the newline-separated output of env.
  size_t pos = 0;

  while (pos < original.size())
    {
      size_t end = original.find ('\n', pos);

      if (end == string::npos)
        end = original.size();

      string entry = original.substr (pos, end - pos);
      size_t eq = entry.find ('=');

      if (eq != string::npos && eq > 0)
        {
          string name = entry.substr (0, eq);
          string value = entry.substr (eq + 1);

          setenv (name.c_str(), value.c_str(), 1);
        }

      pos = end + 1;
    }
}

FileDialog::FileDialog (const string& title, const string& filter, const string& filter_exts, const string& zenity_filename)
{
  // prefer kdialog on KDE, zenity on non-KDE desktops
  string         dialog_type;
  vector<string> dialog_helpers;

  if (is_kde_full_session())
    dialog_helpers = { KDIALOG, ZENITY, YAD };
  else
    dialog_helpers = { ZENITY, YAD, KDIALOG };

  for (auto d : dialog_helpers)
    {
      if (fs::exists (d))
        {
          dialog_type = d;
          break;
        }
    }

  if (dialog_type == "")
    {
      /* shouldn't happen because caller should check have_helpers() */
      fprintf (stderr, "LiquidSFZ: FileDialog: missing helpers: %s, %s or %s, unable to open file dialog\n", KDIALOG, YAD, ZENITY);
      state = ERROR;
      return;
    }

  int pipe_fds[2];
  if (pipe (pipe_fds) == -1)
    {
      state = ERROR;
      fprintf (stderr, "LiquidSFZ: FileDialog: pipe() failed\n");
      return;
    }
  pid = fork();
  if (pid < 0)
    {
      state = ERROR;
      close (pipe_fds[0]);
      close (pipe_fds[1]);
      fprintf (stderr, "LiquidSFZ: FileDialog: fork() failed\n");
      return;
    }
  if (pid == 0) /* child process */
    {
      // replace stdout with pipe
      if (dup2 (pipe_fds[1], STDOUT_FILENO) == -1)
        {
          perror ("LiquidSFZ: FileDialog: dup2() failed");
          exit (127);
        }

      // close remaining pipe fds
      close (pipe_fds[0]);
      close (pipe_fds[1]);

      vector<string> args;
      if (dialog_type == KDIALOG)
        {
          args = { KDIALOG, "--getopenfilename", "--title", title, get_last_start_dir() };
          args.push_back (filter + "(" + filter_exts + ")\nAll Files (*)");
        }
      /* yad and zenity share most command line arguments */
      if (dialog_type == YAD || dialog_type == ZENITY)
        {
          string file_selection_option = (dialog_type == YAD) ? "--file" : "--file-selection";

          args = { dialog_type, file_selection_option, "--title", title };
          if (zenity_filename != "" && fs::exists (zenity_filename))
            {
              args.push_back ("--filename");
              args.push_back (zenity_filename);
            }
          args.push_back ("--file-filter=" + filter + "|" + filter_exts);
          args.push_back ("--file-filter=All Files | *");
        }

      vector<char *> argv;
      for (auto& arg : args)
        argv.push_back (arg.data());
      argv.push_back (nullptr);

      /* Restore the original environment when running in bundled Ardour. */
      restore_ardour_environment();

      execvp (argv[0], argv.data());
      perror ("LiquidSFZ: FileDialog: execvp() failed");

      fprintf (stderr, "LiquidSFZ: failed to execute: ");
      for (auto arg : args)
        fprintf (stderr, "%s ", arg.c_str());
      fprintf (stderr, "\n");

      // should not be reached in normal operation, so exec failed
      exit (127);
    }
  close (pipe_fds[1]); // close pipe write fd
  file_input_fd = pipe_fds[0];
  state = OPEN;
}

FileDialog::~FileDialog()
{
  if (state == OPEN)
    kill (pid, SIGKILL);

  if (state != ERROR)
    {
      assert (file_input_fd >= 0);
      close (file_input_fd);

      int status;
      pid_t exited = waitpid (pid, &status, 0);
      if (exited < 0)
        fprintf (stderr, "LiquidSFZ: FileDialog: waitpid() failed\n");

      if (WIFEXITED (status))
        {
          int exit_status = WEXITSTATUS (status);
          if (exit_status != 0)
            fprintf (stderr, "LiquidSFZ: FileDialog: subprocess failed, exit_status %d\n", exit_status);
        }
      else
        fprintf (stderr, "LiquidSFZ: FileDialog: child didn't exit normally\n");
    }
}

bool
FileDialog::is_open()
{
  return state == OPEN;
}

string
FileDialog::get_filename()
{
  assert (state == OPEN);

  struct pollfd pfd;
  pfd.fd = file_input_fd;
  pfd.events = POLLIN;

  int ret = poll (&pfd, 1, 0); // 0 ms timeout => non-blocking
  if (ret > 0)
    {
      if (pfd.revents & POLLIN)
        {
          FILE *f = fdopen (file_input_fd, "r");
          if (!f)
            {
              state = ERROR;
              perror ("LiquidSFZ: FileDialog: fdopen() failed"); // really should not happen
              return "";
            }
          string filename;
          int ch;
          while ((ch = fgetc (f)) > 0)
            if (ch != '\n')
              filename += ch;
          state = DONE;
          fclose (f);

          set_last_start_dir (filename);

          /* return filename */
          return filename;
        }
      if (pfd.revents & POLLHUP)
        {
          state = DONE;
          return "";
        }
    }
  return "";
}

