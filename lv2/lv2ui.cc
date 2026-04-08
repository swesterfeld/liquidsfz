// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>
#include <signal.h>
#include <sys/wait.h>
#include <filesystem>

#include "pugl/pugl.h"
#include "pugl/gl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "lv2plugin.hh"

#define LIQUIDSFZ_UI_URI      "http://spectmorph.org/plugins/liquidsfz#ui"

namespace fs = std::filesystem;

using std::string;
using std::vector;

class FileDialog
{
  int file_input_fd = -1;
  pid_t pid = -1;
  enum {
    NONE,
    ERROR,
    OPEN,
    DONE
  } state = NONE;

  constexpr static auto KDIALOG = "/usr/bin/kdialog";
  constexpr static auto YAD     = "/usr/bin/yad";
  constexpr static auto ZENITY  = "/usr/bin/zenity";
  constexpr static auto USRSCRIPT  = "/usr/local/bin/sfzselect";

  static inline string     last_start_dir;
  static inline std::mutex last_start_dir_mutex;
  void
  set_last_start_dir (const string& filename)
  {
    std::lock_guard lg (last_start_dir_mutex);
    fs::path fs_path (fs::absolute (filename));
    last_start_dir = fs_path.parent_path().string();
  }
  string
  get_last_start_dir()
  {
    std::lock_guard lg (last_start_dir_mutex);
    std::string start_dir;
    if (last_start_dir != "" && fs::is_directory (last_start_dir))
      return last_start_dir;
    return ".";
  }
  bool is_kde_full_session();
public:
  FileDialog (const string& title, const string& filter, const string& filter_exts, const string& zenity_filename);
  ~FileDialog();

  bool is_open ();
  static bool have_helpers();
  string get_filename();
};

bool
FileDialog::have_helpers()
{
  return fs::exists (KDIALOG) || fs::exists (YAD) || fs::exists (ZENITY) || fs::exists (USRSCRIPT);
}

bool
FileDialog::is_kde_full_session()
{
  char *env = getenv ("KDE_FULL_SESSION");
  return env && (strcmp (env, "true") == 0);
}

FileDialog::FileDialog (const string& title, const string& filter, const string& filter_exts, const string& zenity_filename)
{
  // prefer kdialog on KDE, zenity on non-KDE desktops
  string         dialog_type;
  vector<string> dialog_helpers;

  if (is_kde_full_session())
    dialog_helpers = { USRSCRIPT, KDIALOG, ZENITY, YAD };
  else
    dialog_helpers = { USRSCRIPT,  ZENITY, YAD, KDIALOG };

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
      if (dialog_type == USRSCRIPT) args = { USRSCRIPT };
      if (dialog_type == KDIALOG)
        {
          args = { KDIALOG, "--getopenfilename", "--title", title, get_last_start_dir() };
          args.push_back (filter + "(" + filter_exts + ")\nAll Files (*)");
        }
      /* yad and zenity share the same command line arguments */
      if (dialog_type == YAD || dialog_type == ZENITY)
        {
          args = { dialog_type, "--file-selection", "--title", title };
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

struct LV2UI
{
  ImGuiContext *imgui_ctx = nullptr;
  PuglWorld    *world     = nullptr;
  PuglView     *view      = nullptr;
  LV2Plugin    *plugin    = nullptr;

  LV2UI_Write_Function write_function = nullptr;
  LV2UI_Controller     controller     = nullptr;

  std::unique_ptr<FileDialog> file_dialog;

  int redraw_required_frames = 0;
  bool show_missing_file_dialog_helpers = false;
  double last_time = 0;
  string cache_status;

  float plugin_control_level = 0;
  float progress = -1;

  ~LV2UI();

  PuglStatus on_event (const PuglEvent *event);
  void port_event (uint32_t port_index, uint32_t buffer_size, uint32_t format, const void* buffer);
  void idle();
  ImVec2 render_frame();
  void redraw();
};


LV2UI::~LV2UI()
{
  ImGui::SetCurrentContext (imgui_ctx);

  puglEnterContext(view);
  ImGui_ImplOpenGL3_Shutdown();
  puglLeaveContext(view);

  ImGui::DestroyContext();
  puglFreeView(view);
  puglFreeWorld(world);
}

void
LV2UI::idle()
{
  puglUpdate (world, 0.0);

  ImGui::SetCurrentContext (imgui_ctx);
  if (plugin->redraw_required() || redraw_required_frames)
    {
      if (redraw_required_frames)
        redraw_required_frames--;

      puglObscureView (view);
    }

  if (file_dialog)
    {
      string filename = file_dialog->get_filename();
      if (filename != "")
        {
          plugin->load_threadsafe (filename, 0);
          file_dialog.reset();
          redraw();
        }
      else if (!file_dialog->is_open()) // user closed dialog
        {
          file_dialog.reset();
          redraw();
        }
    }

  float new_progress = plugin->load_progress_threadsafe();
  if (new_progress != progress)
    {
      progress = new_progress;
      redraw();
    }
  string new_cache_status = plugin->cache_status();
  if (new_cache_status != cache_status)
    {
      cache_status = new_cache_status;
      redraw();
    }
}

void
LV2UI::redraw()
{
  /* redraw multiple frames on redraw()
   *   ->give ImGui some time to reach a stable state
   */
  redraw_required_frames = 5;
}

PuglStatus
LV2UI::on_event (const PuglEvent *event)
{
  if (event->type == PUGL_REALIZE || event->type == PUGL_UNREALIZE)
    return PUGL_SUCCESS;

  ImGui::SetCurrentContext (imgui_ctx);
  ImGuiIO& io = ImGui::GetIO();

  switch (event->type)
    {
      case PUGL_CONFIGURE:
        io.DisplaySize = ImVec2 (event->configure.width, event->configure.height);
        glViewport (0, 0, event->configure.width, event->configure.height);
        break;

      case PUGL_CLOSE:
        //state->quit = true;
        break;

      case PUGL_POINTER_IN:
      case PUGL_MOTION:
        io.AddMousePosEvent (event->motion.x, event->motion.y);
        redraw();
        break;

      case PUGL_POINTER_OUT:
        io.AddMousePosEvent (-FLT_MAX, -FLT_MAX); // place mouse pointer "off-screen"
        redraw();
        break;

      case PUGL_BUTTON_PRESS:
      case PUGL_BUTTON_RELEASE:
        {
          int button = 0;
          if (event->button.button == 1) button = 0;      // Left
          else if (event->button.button == 2) button = 2; // Middle
          else if (event->button.button == 3) button = 1; // Right
          io.AddMouseButtonEvent (button, event->type == PUGL_BUTTON_PRESS);
          redraw();
          break;
        }

      case PUGL_SCROLL:
        io.AddMouseWheelEvent (event->scroll.dx, event->scroll.dy);
        redraw();
        break;

      case PUGL_EXPOSE:
        {
          bool old_popup_open = ImGui::IsPopupOpen (nullptr, ImGuiPopupFlags_AnyPopupId);

          ImGui_ImplOpenGL3_NewFrame();
          ImGui::NewFrame();
          ImGui::SetNextWindowSize (io.DisplaySize);
          render_frame();
          ImGui::Render();

          glClearColor (0.1f, 0.1f, 0.1f, 1.0f);
          glClear (GL_COLOR_BUFFER_BIT);
          ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData());

          bool popup_open = ImGui::IsPopupOpen (nullptr, ImGuiPopupFlags_AnyPopupId);
          if (popup_open != old_popup_open)
            redraw();
          break;
        }
      default:
        break;
    }
  return PUGL_SUCCESS;
}

ImVec2
LV2UI::render_frame()
{
  ImGuiIO& io = ImGui::GetIO();

  double current_time = puglGetTime (puglGetWorld (view));
  io.DeltaTime = last_time > 0.0 ? (current_time - last_time) : (1.0f / 60.0f);
  last_time = current_time;

  // 3. Single full-window UI
  ImGui::SetNextWindowPos (ImVec2 (0, 0));

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_AlwaysAutoResize;

  ImGui::Begin ("MainUI", nullptr, flags);

  if (show_missing_file_dialog_helpers)
    {
      // Calculate height of the UI block
      float block_height =
            ImGui::GetTextLineHeightWithSpacing() * 4   // 4 text lines
          + ImGui::GetFrameHeightWithSpacing();         // button

      // Center vertically
      float avail = ImGui::GetContentRegionAvail().y;
      if (avail > block_height)
          ImGui::SetCursorPosY (ImGui::GetCursorPosY() + (avail - block_height) * 0.5f);

      ImGui::TextColored (ImVec4 (1.0f, 0.35f, 0.35f, 1.0f), "Error: File dialog helpers missing, please install one of the following:");

      ImGui::BulletText ("zenity (recommended)");
      ImGui::BulletText ("yad");
      ImGui::BulletText ("kdialog");

      // Center the button
      float width = 120.0f;
      ImGui::SetCursorPosX ((ImGui::GetWindowSize().x - width) * 0.5f);

      if (ImGui::Button ("OK", ImVec2 (width, 0)))
        {
          show_missing_file_dialog_helpers = false;
          redraw();
        }
    }
  else
    {
      // Get the standard height for a widget in the current style
      float widget_height = ImGui::GetFrameHeight();
      if (progress >= 0)
        {
          ImGui::ProgressBar (progress / 100, ImVec2 (-FLT_MIN, widget_height));
        }
      else
        {
          ImGui::BeginDisabled (file_dialog != nullptr);
          if (ImGui::Button ("Load SFZ/XML File...", ImVec2 (-FLT_MIN, widget_height)))
            {
              if (!file_dialog->have_helpers())
                {
                  show_missing_file_dialog_helpers = true;
                }
              else
                {
                  file_dialog = std::make_unique<FileDialog> ("LiquidSFZ: Select SFZ/XML File", "SFZ/XML Files", "*.sfz *.xml", plugin->filename());

                  if (!file_dialog->is_open())
                    {
                      /* something went wrong creating the filedialog */
                      file_dialog.reset();
                    }
                }
              redraw();
            }
          ImGui::EndDisabled();
        }
      if (ImGui::BeginTable ("PropertyTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
          // Column 0: auto-sized to content
          ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed);

          // Column 1: stretch, takes remaining space
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex (0);
          ImGui::AlignTextToFramePadding();
          ImGui::Text("Output Level:");
          ImGui::TableSetColumnIndex (1);
          ImGui::SetNextItemWidth (-FLT_MIN);
          if (ImGui::SliderFloat("##slider1", &plugin_control_level, -80.f, 20.f))
            {
              write_function (controller, LV2Plugin::LEVEL, sizeof (float), 0, &plugin_control_level);
            }

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex (0);
          ImGui::AlignTextToFramePadding();
          ImGui::Text ("Program:");
          ImGui::TableSetColumnIndex (1);
          ImGui::SetNextItemWidth (-FLT_MIN);

          vector<string> programs = plugin->programs();
          string filename = plugin->filename();
          if (programs.size() == 0)
            {
              std::filesystem::path filepath = filename;
              programs = { filepath.stem().string() };
            }

          /* if program is invalid (> length of programs), display first item as
           * selected, this will not change the invalid program until the user
           * touches the combo box
           */
          int program = std::min<int> (plugin->program(), programs.size());
          if (ImGui::BeginCombo ("##programs", programs[program].c_str()))
            {
              for (int i = 0; i < (int) programs.size(); i++)
                {
                  ImGui::PushID (i);

                  bool is_selected = (program == i);
                  if (ImGui::Selectable (programs[i].c_str(), is_selected))
                    {
                      /* load new program (same file) */
                      plugin->load_threadsafe (filename, i);
                    }

                  if (is_selected)
                    ImGui::SetItemDefaultFocus();

                  ImGui::PopID();
                }
              ImGui::EndCombo();
            }

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex (0);
          ImGui::AlignTextToFramePadding();
          ImGui::Text ("Status:");
          ImGui::TableSetColumnIndex (1);
          ImGui::TextUnformatted (plugin->status().c_str());

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex (0);
          ImGui::AlignTextToFramePadding();
          ImGui::Text ("Cache:");
          ImGui::TableSetColumnIndex (1);
          ImGui::TextUnformatted (cache_status.c_str());

          ImGui::EndTable();
        }
    }
  ImVec2 required_size = ImGui::GetWindowSize();

  ImGui::End();
  return required_size;
}

void
LV2UI::port_event (uint32_t port_index, uint32_t buffer_size, uint32_t format, const void* buffer)
{
  if (port_index == LV2Plugin::LEVEL)
    {
      plugin_control_level = *(const float *) buffer;
      redraw();
    }
}

static LV2UI_Handle
instantiate (const LV2UI_Descriptor*   descriptor,
             const char*               plugin_uri,
             const char*               bundle_path,
             LV2UI_Write_Function      write_function,
             LV2UI_Controller          controller,
             LV2UI_Widget*             widget,
             const LV2_Feature* const* features)
{
  LV2Plugin *plugin = nullptr;
  PuglNativeView parent_win_id = 0;
  LV2_URID_Map* map = nullptr;
  LV2UI_Resize *ui_resize = nullptr;

  for (int i = 0; features[i]; i++)
    {
      if (!strcmp (features[i]->URI, LV2_URID__map))
        {
          map = (LV2_URID_Map*)features[i]->data;
        }
      else if (!strcmp (features[i]->URI, LV2_UI__parent))
        {
          parent_win_id = (PuglNativeView)features[i]->data;
        }
      else if (!strcmp (features[i]->URI, LV2_UI__resize))
        {
          ui_resize = (LV2UI_Resize *) features[i]->data;
        }
      else if (!strcmp (features[i]->URI, LV2_INSTANCE_ACCESS_URI))
        {
          plugin = (LV2Plugin *) features[i]->data;
        }
    }
  if (!map)
    {
      return nullptr; // host bug, we need this feature
    }
  LV2UI *ui = new LV2UI;
  ui->plugin = plugin;

  // 1. Setup Pugl World and View
  PuglWorld* world = puglNewWorld (PUGL_MODULE, 0);
  PuglView* view = puglNewView (world);
  ui->world = world;
  ui->view = view;
  ui->write_function = write_function;
  ui->controller = controller;

  // 2. Setup ImGui
  IMGUI_CHECKVERSION();
  ImGuiContext* ctx = ImGui::CreateContext();
  ui->imgui_ctx = ctx;
  ImGui::SetCurrentContext (ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; // don't write imgui.ini
  ImGui::StyleColorsDark();

  /* Scaling */

  float scale_factor = puglGetScaleFactor (view);

  // Scale all UI elements (buttons, sliders, padding)
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes (scale_factor);

  // Scale the default font
  float base_font_size = 13.f;
  int font_size = lrint (base_font_size * scale_factor);

  io.Fonts->AddFontFromFileTTF ((string (bundle_path) + "/dejavu-lgc-sans.ttf").c_str(), font_size);

  /* Compute required window width / height */
  float frame_height = font_size + (style.FramePadding.y * 2.0f);

  int window_width = 400 * scale_factor;
  int window_height =
    (
      style.WindowPadding.y +
      frame_height +
      style.ItemSpacing.y +
      style.CellPadding.y +
      frame_height +
      2 * style.CellPadding.y +
      frame_height +
      2 * style.CellPadding.y +
      frame_height +
      2 * style.CellPadding.y +
      frame_height +
      style.CellPadding.y +
      style.WindowPadding.y
    );

  /* setup window */

  // XXXpuglSetClassName(world, "ImGui Minimal Example");
  // XXXpuglSetWindowTitle(view, "ImGui Minimal Example");

  puglSetSizeHint (view, PUGL_MIN_SIZE, window_width, window_height);
  puglSetSizeHint (view, PUGL_MAX_SIZE, window_width, window_height);
  puglSetSizeHint (view, PUGL_DEFAULT_SIZE, window_width, window_height);
  puglSetViewHint (view, PUGL_RESIZABLE, false);
  puglSetBackend (view, puglGlBackend());

  // Request OpenGL 3.3 Core Profile (adjust to match your ImGui glsl version)
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetParent (view, parent_win_id);

  // Bind our app state and event handler
  puglSetHandle (view, ui);
  puglSetEventFunc (view,
    [] (PuglView *view, const PuglEvent *event)
      {
        LV2UI *ui = (LV2UI *) puglGetHandle (view);
        return ui->on_event (event);
      });

  if (puglRealize (view) != PUGL_SUCCESS)
    {
      fprintf (stderr, "failed to create pugl view\n");
      return nullptr;
    }
  puglShow (view, PUGL_SHOW_PASSIVE);

  // Note: To call OpenGL initialization outside of a Pugl event,
  // we must manually make the context current first.
  puglEnterContext (view);
  ImGui_ImplOpenGL3_Init ("#version 130");
  puglLeaveContext (view);

  if (ui_resize)
    ui_resize->ui_resize (ui_resize->handle, window_width, window_height);

  *widget = (void *) puglGetNativeView (view);
  return ui;
}

static void
cleanup (LV2UI_Handle handle)
{
  LV2UI *ui = static_cast <LV2UI *> (handle);
  delete ui;
}

static void
port_event (LV2UI_Handle handle,
            uint32_t     port_index,
            uint32_t     buffer_size,
            uint32_t     format,
            const void*  buffer)
{
  LV2UI *ui = static_cast <LV2UI *> (handle);
  ui->port_event (port_index, buffer_size, format, buffer);
}

static int
idle (LV2UI_Handle handle)
{
  LV2UI* ui = (LV2UI*) handle;

  ui->idle();
  return 0;
}

static const LV2UI_Idle_Interface idle_iface = { idle };

static const void*
extension_data (const char* uri)
{
  // could implement show interface

  if (!strcmp(uri, LV2_UI__idleInterface)) {
    return &idle_iface;
  }
  return nullptr;
}

static const LV2UI_Descriptor descriptor = {
  LIQUIDSFZ_UI_URI,
  instantiate,
  cleanup,
  port_event,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor*
lv2ui_descriptor (uint32_t index)
{
  switch (index)
    {
      case 0:   return &descriptor;
      default:  return NULL;
    }
}
