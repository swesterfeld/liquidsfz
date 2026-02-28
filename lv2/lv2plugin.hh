// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <string>
#include <mutex>
#include <atomic>

#include "liquidsfz.hh"

#if __has_include (<lv2/core/lv2.h>)
// new versions of LV2 use different location for headers
#include "lv2/atom/atom.h"
#include "lv2/atom/forge.h"
#include "lv2/midi/midi.h"
#include "lv2/urid/urid.h"
#include "lv2/patch/patch.h"
#include "lv2/state/state.h"
#include "lv2/worker/worker.h"
#else
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"
#endif

#include "lv2_midnam.h"

class RTMutex
{
  std::atomic_flag locked_flag = ATOMIC_FLAG_INIT;
public:
  bool try_lock();
  void wait_for_lock();
  void unlock();
};

class LV2Plugin
{
public:
  enum PortIndex
  {
    MIDI_IN    = 0,
    LEFT_OUT   = 1,
    RIGHT_OUT  = 2,
    LEVEL      = 3,
    FREEWHEEL  = 4,
    NOTIFY     = 5
  };

private:
  // URIs
  struct URIS
  {
    LV2_URID atom_Blank;
    LV2_URID atom_Object;
    LV2_URID atom_URID;
    LV2_URID atom_Path;
    LV2_URID atom_Int;
    LV2_URID midi_MidiEvent;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_value;
    LV2_URID state_StateChanged;

    LV2_URID liquidsfz_sfzfile;
    LV2_URID liquidsfz_program;
  } uris;

  // Port buffers
  const LV2_Atom_Sequence *midi_in = nullptr;
  float                   *left_out = nullptr;
  float                   *right_out = nullptr;
  const float             *level = nullptr;
  const float             *freewheel = nullptr;
  LV2_Atom_Sequence       *notify_port = nullptr;

  std::string              current_filename;
  std::string              current_status;
  int                      current_program = 0;
  std::vector<std::string> current_programs;
  bool                     file_or_program_changed = false;

  bool                     load_in_progress = false;
  bool                     ui_redraw_required = false;
  RTMutex                  rt_mutex;
  bool                     inform_ui = false;
  static constexpr int     command_load = 0x10001234; // just some random number
  float                    old_level = 1000;          // outside range [-80:20]
  float                    old_freewheel = -1;        // outside boolean range [0:1]
  std::atomic<float>       load_progress = -1;

  LV2_Worker_Schedule     *schedule = nullptr;
  LV2_Midnam              *midnam = nullptr;
  LV2_Atom_Forge           forge;
  LiquidSFZ::Synth         synth;
  std::string              midnam_model;
  std::string              midnam_str;
  std::mutex               midnam_str_mutex;
public:
  LV2Plugin (int rate, LV2_URID_Map *map, LV2_Worker_Schedule *schedule, LV2_Midnam *midnam);

  void write_state_changed();
  char *get_midnam_str();
  char *get_midnam_model();
  void work (LV2_Worker_Respond_Function respond, LV2_Worker_Respond_Handle handle, uint32_t size, const void *data);
  void work_response (uint32_t size, const void *data);
  float db_to_factor (float db);
  void activate();
  void run (uint32_t n_samples);
  void connect_port (uint32_t port, void* data);
  LV2_State_Status save (LV2_State_Store_Function store, LV2_State_Handle handle, const LV2_Feature* const* features);
  LV2_State_Status restore (LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle, const LV2_Feature* const* features);

  void load_threadsafe (const std::string& filename, uint program);
  float load_progress_threadsafe() const;
  std::vector<std::string> programs();
  int program();
  std::string filename();
  std::string status();
  bool redraw_required();
};
