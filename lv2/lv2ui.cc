// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#include "pugl/pugl.h"
#include "pugl/gl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "lv2/ui/ui.h"
#include "lv2/instance-access/instance-access.h"
#include "lv2plugin.hh"

#define LIQUIDSFZ_UI_URI      "http://spectmorph.org/plugins/liquidsfz#ui"

using std::string;

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
public:
  FileDialog();
  ~FileDialog();

  bool is_open ();
  string get_filename();
};

FileDialog::FileDialog()
{
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

      static char *argv[3] {
        (char *) "/usr/bin/kdialog",
        (char *) "--getopenfilename",
        nullptr
      };
      execvp (argv[0], argv);
      perror ("LiquidSFZ: FileDialog: execvp() failed");

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

  int ret = poll(&pfd, 1, 0); // 0 ms timeout => non-blocking
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
  PuglWorld *world = nullptr;
  PuglView *view = nullptr;
  LV2UI_Resize *ui_resize = nullptr;
  std::unique_ptr<FileDialog> file_dialog;
  LV2Plugin *plugin = nullptr;
  LV2UI_Write_Function write_function = nullptr;
  LV2UI_Controller controller = nullptr;

  bool configured = false;
  double last_time = 0;
  int frame_count = 0;

  float plugin_control_level = 0;
  int test_item1 = 0;

  ~LV2UI();

  PuglStatus on_event (const PuglEvent *event);
  void port_event (uint32_t port_index, uint32_t buffer_size, uint32_t format, const void* buffer);
  void idle();
  ImVec2 render_frame();
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

  if (frame_count < 50)
    {
      /* HACK: ImGui rendering is not correct for the first few frames,
       * so we enforce redrawing a few frames after initialization
       */
      puglObscureView (view);
      frame_count++;
    }

  if (file_dialog)
    {
      string s = file_dialog->get_filename();
      if (s != "")
        {
          plugin->load_threadsafe (s);
          file_dialog.reset();
        }
      else if (!file_dialog->is_open()) // user closed dialog
        file_dialog.reset();
    }
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
        // Update display size
        io.DisplaySize = ImVec2 (event->configure.width, event->configure.height);
        // Pugl automatically binds the GL context before configure/expose events
        glViewport (0, 0, event->configure.width, event->configure.height);
        printf ("rsz %d %d\n", event->configure.width, event->configure.height);
        configured = true;
        break;

      case PUGL_CLOSE:
        //state->quit = true;
        break;

      case PUGL_MOTION:
        io.AddMousePosEvent (event->motion.x, event->motion.y);
        puglObscureView (view);
        break;

      case PUGL_POINTER_OUT:
        io.AddMousePosEvent (-FLT_MAX, -FLT_MAX); // place mouse pointer "off-screen"
        puglObscureView (view);
        break;

      case PUGL_BUTTON_PRESS:
      case PUGL_BUTTON_RELEASE:
        {
          int button = 0;
          if (event->button.button == 1) button = 0;      // Left
          else if (event->button.button == 2) button = 2; // Middle
          else if (event->button.button == 3) button = 1; // Right
          io.AddMouseButtonEvent (button, event->type == PUGL_BUTTON_PRESS);
          puglObscureView (view);
          break;
        }

      case PUGL_SCROLL:
        io.AddMouseWheelEvent (event->scroll.dx, event->scroll.dy);
        puglObscureView (view);
        break;

      case PUGL_EXPOSE:
        {
          ImGui_ImplOpenGL3_NewFrame();
          ImGui::NewFrame();
          ImGui::SetNextWindowSize (io.DisplaySize);
          render_frame();
          ImGui::Render();

          glClearColor (0.1f, 0.1f, 0.1f, 1.0f);
          glClear (GL_COLOR_BUFFER_BIT);
          ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData());
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

  ImGui::Begin("MainUI", nullptr, flags);

  ImGui::BeginDisabled (file_dialog != nullptr);
  if (ImGui::Button ("Load SFZ/XML File...", ImVec2 (-FLT_MIN, 0)))
    {
      file_dialog = std::make_unique<FileDialog>();

      if (!file_dialog->is_open())
        {
          /* something went wrong creating the filedialog */
          file_dialog.reset();
        }
    }
  ImGui::EndDisabled();
  if (ImGui::BeginTable("PropertyTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
      const char* items1[] = { "Option A", "Option B", "Option C" };

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
      ImGui::Text("Select Option:");
      ImGui::TableSetColumnIndex (1);
      ImGui::SetNextItemWidth (-FLT_MIN);
      ImGui::Combo("##combo1", &test_item1, items1, IM_ARRAYSIZE(items1));

      ImGui::EndTable();
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
      puglObscureView (view);
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
          ui_resize = (LV2UI_Resize*)features[i]->data;
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
  ui->ui_resize = ui_resize;
  ui->write_function = write_function;
  ui->controller = controller;

  // XXXpuglSetClassName(world, "ImGui Minimal Example");
  // XXXpuglSetWindowTitle(view, "ImGui Minimal Example");
  puglSetSizeHint (view, PUGL_MIN_SIZE, 64, 64);
  puglSetSizeHint (view, PUGL_MAX_SIZE, 2048, 2048);
  puglSetSizeHint (view, PUGL_DEFAULT_SIZE, 400, 100);
  puglSetViewHint (view, PUGL_RESIZABLE, false);
  puglSetBackend (view, puglGlBackend());

  // Request OpenGL 3.3 Core Profile (adjust to match your ImGui glsl version)
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetParent (view, parent_win_id);

  // Bind our app state and event handler
  puglSetHandle (view, ui); // FIXME
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

  // 2. Setup ImGui
  IMGUI_CHECKVERSION();
  ImGuiContext* ctx = ImGui::CreateContext();
  ui->imgui_ctx = ctx;
  ImGui::SetCurrentContext (ctx);
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  ImGui::StyleColorsDark();

  // --- SCALING LOGIC ---
  float scale_factor = puglGetScaleFactor (view);

  // 1. Scale all UI elements (buttons, sliders, padding)
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(scale_factor);

  // 2. Scale the default font
  double base_font_size = 12.f;
  io.Fonts->AddFontFromFileTTF("/home/stefan/.local/share/spectmorph/fonts/dejavu-lgc-sans-bold.ttf", base_font_size * scale_factor);
  //puglSetSizeHint (view, PUGL_CURRENT_SIZE, 400 * scale_factor, 400 * scale_factor);
  printf ("%f\n", 400 * scale_factor);
  // ---------------------

  // Note: To call OpenGL initialization outside of a Pugl event,
  // we must manually make the context current first.
  puglEnterContext (view);
  ImGui_ImplOpenGL3_Init ("#version 130");
  ImVec2 required_size;
  for (int frames = 0; frames < 2; frames++)
    {
      ImGui_ImplOpenGL3_NewFrame();
      io.DisplaySize = ImVec2 (400 * scale_factor, 400 * scale_factor);

      // ImGui doesn't return correct size on first frame, so we need to render twice
      ImGui::NewFrame();
      ImGui::SetNextWindowSize (ImVec2 (400 * scale_factor, 0));
      required_size = ui->render_frame();
      ImGui::EndFrame();
    }
  puglLeaveContext (view);
  puglSetSizeHint (view, PUGL_CURRENT_SIZE, required_size.x, required_size.y);

  while (!ui->configured)
    {
      usleep (50000);
      ui->idle();
    }

  *widget = (void *) puglGetNativeView (view);
  return ui;
#if 0
  sm_plugin_init();

  LV2_DEBUG ("instantiate called for ui\n");

  LV2Common::detect_repeated_features (features);

  LV2UI *ui = new LV2UI (parent_win_id, ui_resize, plugin);
  ui->init_map (map);

  /* set initial window size */
  ui->on_update_window_size();

  return ui;
#endif
}

static void
cleanup (LV2UI_Handle handle)
{
  printf ("cleanup\n");
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
