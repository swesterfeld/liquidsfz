// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl-2.1.html

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "pugl/pugl.h"
#include "pugl/gl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "lv2/ui/ui.h"
#include "lv2/instance-access/instance-access.h"

#define LIQUIDSFZ_UI_URI      "http://spectmorph.org/plugins/liquidsfz#ui"

class LV2Plugin;

struct LV2UI
{
  PuglWorld *world = nullptr;
  PuglView *view = nullptr;

  void
  idle()
  {
    puglUpdate (world, 0.0);
    puglObscureView (view); // FIXME: should only update on change
  }
};

PuglStatus
onEvent (PuglView *view, const PuglEvent *event)
{
  if (event->type == PUGL_REALIZE || event->type == PUGL_UNREALIZE)
    return PUGL_SUCCESS;

  static struct State { //XXX
    int current_item1{};
    int current_item2{};
  } s;
  auto state = &s;

  ImGuiIO& io = ImGui::GetIO();

  switch (event->type)
    {
      case PUGL_CONFIGURE:
        // Update display size
        io.DisplaySize = ImVec2 (event->configure.width, event->configure.height);
        // Pugl automatically binds the GL context before configure/expose events
        glViewport (0, 0, event->configure.width, event->configure.height);
        printf ("rsz %d %d\n", event->configure.width, event->configure.height);
        break;
      case PUGL_CLOSE:
        //state->quit = true;
        break;

      case PUGL_MOTION:
        io.AddMousePosEvent (event->motion.x, event->motion.y);
        break;

      case PUGL_BUTTON_PRESS:
      case PUGL_BUTTON_RELEASE:
        {
          int button = 0;
          if (event->button.button == 1) button = 0;      // Left
          else if (event->button.button == 2) button = 2; // Middle
          else if (event->button.button == 3) button = 1; // Right
          io.AddMouseButtonEvent (button, event->type == PUGL_BUTTON_PRESS);
          break;
        }

      case PUGL_SCROLL:
        io.AddMouseWheelEvent (event->scroll.dx, event->scroll.dy);
        break;

      case PUGL_EXPOSE:
        {
          // 1. Calculate Delta Time
          double current_time = puglGetTime (puglGetWorld (view));
          //io.DeltaTime = state->last_time > 0.0 ? (float)(current_time - state->last_time) : (1.0f / 60.0f);
          //state->last_time = current_time;

          // 2. Start ImGui Frame
          ImGui_ImplOpenGL3_NewFrame();
          ImGui::NewFrame();

          static int width = 0; //XXX
          static int height = 0;
          // 3. Single full-window UI
          ImGui::SetNextWindowPos (ImVec2 (0, 0));
          ImGui::SetNextWindowSize (io.DisplaySize);
#if 0
          if (width)
            ImGui::SetNextWindowSize (io.DisplaySize);
          else
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0), ImGuiCond_Always);
#endif
          ImGuiWindowFlags flags =
              ImGuiWindowFlags_NoTitleBar |
              ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove |
              ImGuiWindowFlags_NoCollapse |
              ImGuiWindowFlags_NoBringToFrontOnFocus;
#if 0
          ImGui::Begin("MainUI", nullptr, flags);
#endif
          ImGui::Begin("CalcSize", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
#if 0
          ImGui::Begin("MainUI", nullptr, ImGuiWindowFlags_NoResize);
#endif

          if (ImGui::BeginTable("PropertyTable", 2, ImGuiTableFlags_SizingStretchProp))
            {
              const char* items1[] = { "Option A", "Option B", "Option C" };
              const char* items2[] = { "Red", "Green", "Blue" };

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex (0);
              ImGui::AlignTextToFramePadding();
              ImGui::Text("Select Option:");
              ImGui::TableSetColumnIndex (1);
              ImGui::SetNextItemWidth (-FLT_MIN);      // Fill remaining column
              ImGui::Combo("##combo1", &state->current_item1, items1, IM_ARRAYSIZE(items1));

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex (0);
              ImGui::AlignTextToFramePadding();
              ImGui::Text("Select Color:");
              ImGui::TableSetColumnIndex (1);
              ImGui::SetNextItemWidth (-FLT_MIN);      // Fill remaining column
              ImGui::Combo("##combo2", &state->current_item2, items2, IM_ARRAYSIZE(items2));

              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex (0);
              ImGui::AlignTextToFramePadding();
              ImGui::Text("Select Color:");
              ImGui::TableSetColumnIndex (1);
              ImGui::SetNextItemWidth (-FLT_MIN);      // Fill remaining column
              ImGui::Combo("##combo3", &state->current_item2, items2, IM_ARRAYSIZE(items2));
              ImGui::EndTable();
            }
          ImVec2 required_size = ImGui::GetWindowSize();
          static int frame_count = 0; // XXX
          if (frame_count++ == 1)
            {
              width = required_size.x;
              height = required_size.y;
              printf ("required_size: %f %f\n", required_size.x, required_size.y);
            }
          ImGui::End();

          // 4. Rendering
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
  fprintf (stderr, "parent_win_id=%ld\n", parent_win_id);
  LV2UI *ui = new LV2UI;

  // 1. Setup Pugl World and View
  PuglWorld* world = puglNewWorld (PUGL_MODULE, 0);
  PuglView* view = puglNewView (world);
  ui->world = world;
  ui->view = view;

  // XXXpuglSetClassName(world, "ImGui Minimal Example");
  // XXXpuglSetWindowTitle(view, "ImGui Minimal Example");
  puglSetSizeHint (view, PUGL_MIN_SIZE, 128, 128);
  puglSetSizeHint (view, PUGL_MAX_SIZE, 2048, 2048);
  puglSetSizeHint (view, PUGL_DEFAULT_SIZE, 800, 600);
  puglSetViewHint (view, PUGL_RESIZABLE, true);
  puglSetBackend (view, puglGlBackend());

  // Request OpenGL 3.3 Core Profile (adjust to match your ImGui glsl version)
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint (view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetParent (view, parent_win_id);

  // Bind our app state and event handler
  //puglSetHandle (view, &state); // FIXME
  puglSetEventFunc (view, onEvent);

  if (puglRealize (view) != PUGL_SUCCESS)
    {
      fprintf (stderr, "failed to create pugl view\n");
      return nullptr;
    }
  puglShow (view, PUGL_SHOW_PASSIVE);

  // 2. Setup ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
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
  puglSetSizeHint (view, PUGL_CURRENT_SIZE, 400 * scale_factor, 400 * scale_factor);
  // ---------------------

  // Note: To call OpenGL initialization outside of a Pugl event,
  // we must manually make the context current first.
  puglEnterContext(view);
  ImGui_ImplOpenGL3_Init("#version 130");
  puglLeaveContext(view);

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
#if 0
  LV2_DEBUG ("cleanup called for ui\n");

  LV2UI *ui = static_cast <LV2UI *> (handle);
  delete ui;

  sm_plugin_cleanup();
#endif
}

static void
port_event(LV2UI_Handle handle,
           uint32_t     port_index,
           uint32_t     buffer_size,
           uint32_t     format,
           const void*  buffer)
{
#if 0
  LV2UI *ui = static_cast <LV2UI *> (handle);
  ui->port_event (port_index, buffer_size, format, buffer);
#endif

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
