// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "filedialog.hh"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

using std::string;
using std::vector;
namespace fs = std::filesystem;

namespace
{

constexpr auto KDIALOG = "/usr/bin/kdialog";
constexpr auto YAD     = "/usr/bin/yad";
constexpr auto ZENITY  = "/usr/bin/zenity";

string last_start_dir;
std::mutex last_start_dir_mutex;

string
get_last_start_dir()
{
  std::lock_guard lg (last_start_dir_mutex);
  std::error_code error;
  return fs::is_directory (last_start_dir, error) ? last_start_dir : ".";
}

void
set_last_start_dir (const string& filename)
{
  std::error_code error;
  auto path = fs::absolute (filename, error);
  if (!error)
    {
      std::lock_guard lg (last_start_dir_mutex);
      last_start_dir = path.parent_path().string();
    }
}

vector<string>
environment_snapshot()
{
  // procfs exposes the environment at exec time, not later setenv() changes.
  // Reading it avoids traversing glibc's concurrently modified pointer array.
  const auto close_file = [] (FILE *file) { fclose (file); };
  std::unique_ptr<FILE, decltype (close_file)> file (fopen ("/proc/self/environ", "re"), close_file);
  if (file)
    {
      string contents;
      char buffer[4096];
      size_t count;
      while ((count = fread (buffer, 1, sizeof (buffer), file.get())) > 0)
        contents.append (buffer, count);
      if (!ferror (file.get()))
        {
          vector<string> env;
          size_t pos = 0;
          while (pos < contents.size())
            {
              size_t end = contents.find ('\0', pos);
              if (end == string::npos)
                end = contents.size();
              if (end > pos)
                env.push_back (contents.substr (pos, end - pos));
              pos = end + 1;
            }
          return env;
        }
    }

  // Compatibility fallback: concurrent environment changes are not safe here.
  //  -> reading environ while some other thread calls setenv() can crash
  vector<string> env;
  if (char **entry = environ)
    while (const char *value = *entry++)
      env.emplace_back (value);
  return env;
}

const char *
environment_value (const vector<string>& env, const string& name)
{
  const string prefix = name + "=";
  for (const auto& entry : env)
    if (entry.compare (0, prefix.size(), prefix) == 0)
      return entry.c_str() + prefix.size();
  return nullptr;
}

vector<string>
helper_environment (const vector<string>& snapshot)
{
  const char *bundled = environment_value (snapshot, "ARDOUR_BUNDLED");
  const char *saved   = environment_value (snapshot, "PREBUNDLE_ENV");
  vector<string> env;
  if (bundled && strcmp (bundled, "true") == 0 && saved && *saved)
    {
      // Ardour saves the original environment as newline-separated entries.
      // Build a child-only copy; never change the host's environment.
      string original (saved);
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
              // Match setenv(..., 1): the last entry for a name wins.
              string prefix = entry.substr (0, eq + 1);
              env.erase (std::remove_if (env.begin(), env.end(), [&] (const string& value)
                { return value.compare (0, prefix.size(), prefix) == 0; }), env.end());
              env.push_back (entry);
            }
          pos = end + 1;
        }
    }
  else
    return snapshot;
  return env;
}
}

bool
FileDialog::have_helpers()
{
  return fs::exists (KDIALOG) || fs::exists (YAD) || fs::exists (ZENITY);
}

bool
FileDialog::is_kde_full_session()
{
  const char *env = environment_value (environment, "KDE_FULL_SESSION");
  return env && (strcmp (env, "true") == 0);
}

FileDialog::FileDialog (const string& title, const string& filter, const string& filter_exts, const string& zenity_filename)
  : environment (environment_snapshot())
{
  vector<string> helpers = is_kde_full_session()
                        ? vector<string> { KDIALOG, ZENITY, YAD }
                        : vector<string> { ZENITY, YAD, KDIALOG };
  for (const auto& helper : helpers)
    {
      if (!fs::exists (helper))
        continue;

      vector<string> args;
      if (helper == KDIALOG)
        {
          args = { helper, "--getopenfilename", "--title", title, get_last_start_dir(),
                   filter + "(" + filter_exts + ")\nAll Files (*)" };
        }
      else // yad and zenity share most command line arguments
        {
          args = { helper, helper == YAD ? "--file" : "--file-selection", "--title", title };
          if (!zenity_filename.empty() && fs::exists (zenity_filename))
            {
              args.push_back ("--filename");
              args.push_back (zenity_filename);
            }
          args.push_back ("--file-filter=" + filter + "|" + filter_exts);
          args.push_back ("--file-filter=All Files | *");
        }
      spawn (std::move (args));
      return;
    }
  fprintf (stderr, "LiquidSFZ: FileDialog: missing helpers: %s, %s or %s\n", KDIALOG, YAD, ZENITY);
}

FileDialog::FileDialog (const vector<string>& args)
  : environment (environment_snapshot())
{
  spawn (args);
}

void
FileDialog::spawn (vector<string> args)
{
  if (args.empty())
    return;

  auto env = helper_environment (environment);
  vector<char *> argv, envp;

  for (auto& arg : args)
    argv.push_back (arg.data());
  argv.push_back (nullptr);

  for (auto& entry : env)
    envp.push_back (entry.data());
  envp.push_back (nullptr);

  int pipe_fds[2];
  if (pipe2 (pipe_fds, O_CLOEXEC) < 0)
    {
      perror ("LiquidSFZ: FileDialog: pipe2() failed");
      return;
    }

  // Keep both ends away from stdin/stdout/stderr, even in a host with closed
  // standard descriptors. Only our read end should be nonblocking.
  int error = 0;
  for (auto& fd : pipe_fds)
    if (fd <= STDERR_FILENO)
      {
        int moved = fcntl (fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        if (moved < 0)
          { error = errno; break; }
        close (fd);
        fd = moved;
      }
  if (!error && fcntl (pipe_fds[0], F_SETFL, O_NONBLOCK) < 0)
    error = errno;

  posix_spawn_file_actions_t actions;
  const int init_error = posix_spawn_file_actions_init (&actions);
  if (!error)
    error = init_error;
  if (!error)
    error = posix_spawn_file_actions_adddup2 (&actions, pipe_fds[1], STDOUT_FILENO);
  if (!error)
    error = posix_spawn_file_actions_addclose (&actions, pipe_fds[0]);
  if (!error)
    error = posix_spawn_file_actions_addclose (&actions, pipe_fds[1]);
  pid_t child = -1;
  if (!error)
    error = posix_spawn (&child, argv[0], &actions, nullptr, argv.data(), envp.data());
  if (!init_error)
    posix_spawn_file_actions_destroy (&actions);
  close (pipe_fds[1]);

  if (error)
    {
      close (pipe_fds[0]);
      fprintf (stderr, "LiquidSFZ: FileDialog: cannot start %s: %s\n", argv[0], strerror (error));
      return;
    }
  pid = child;
  file_input_fd = pipe_fds[0];
  open = true;
}

FileDialog::~FileDialog()
{
  if (file_input_fd >= 0)
    close (file_input_fd);
  if (pid > 0)
    {
      // Only early destruction (UI teardown) reaches this path. Normal dialog
      // completion is reaped without waiting in get_filename(). Kill before
      // waiting so teardown never waits for user interaction or helper output.
      kill (pid, SIGKILL);
      while (waitpid (pid, nullptr, 0) < 0 && errno == EINTR)
        {}
    }
}

string
FileDialog::get_filename()
{
  if (!open)
    return "";

  if (file_input_fd >= 0)
    {
      // One bounded read per UI tick, including when the helper writes only
      // part of a filename or closes stdout before exiting.
      char buffer[4096];
      ssize_t count = read (file_input_fd, buffer, sizeof (buffer));
      if (count > 0)
        output.append (buffer, count);
      else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        {
          if (count < 0)
            {
              perror ("LiquidSFZ: FileDialog: read() failed");
              failed = true;
              if (pid > 0)
                kill (pid, SIGKILL);
            }
          close (file_input_fd);
          file_input_fd = -1;
        }
    }

  if (pid > 0)
    {
      int status = 0;
      pid_t exited = waitpid (pid, &status, WNOHANG);
      if (exited == pid)
        {
          failed = failed || !WIFEXITED (status) || WEXITSTATUS (status) != 0;
          pid = -1;
        }
      else if (exited < 0 && errno != EINTR)
        {
          perror ("LiquidSFZ: FileDialog: waitpid() failed");
          failed = true;
          pid = -1;
        }
    }

  // Keep ownership until both the pipe is drained and the child is reaped.
  if (file_input_fd >= 0 || pid > 0)
    return "";

  open = false;
  if (failed)
    return "";
  output.erase (std::remove (output.begin(), output.end(), '\n'), output.end());
  if (!output.empty())
    set_last_start_dir (output);
  return output;
}
