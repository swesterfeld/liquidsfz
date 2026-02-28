// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "midnam.hh"
#include "log.hh"
#include "lv2plugin.hh"

#include <string>
#include <mutex>
#include <filesystem>

#include <math.h>

#define LIQUIDSFZ_URI      "http://spectmorph.org/plugins/liquidsfz"

#ifndef LV2_STATE__StateChanged
#define LV2_STATE__StateChanged LV2_STATE_PREFIX "StateChanged"
#endif

using namespace LiquidSFZ;

using std::string;
using std::vector;

using LiquidSFZInternal::string_printf;

#undef LIQUIDSFZ_LV2_DEBUG

#ifdef LIQUIDSFZ_LV2_DEBUG
void debug (const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

void
debug (const char *format, ...)
{
  static std::mutex debug_mutex;
  static FILE *out = nullptr;

  std::lock_guard lg (debug_mutex);

  if (!out)
    out = fopen ("/tmp/liquidsfz.log", "w");

  if (out)
    {
      va_list ap;

      va_start (ap, format);
      vfprintf (out, format, ap);
      fflush (out);
      va_end (ap);
    }
}
#else
void
debug (const char *format, ...)
{
}
#endif

class RTMutexWaitLockGuard
{
  RTMutex& mutex_;
public:
  explicit RTMutexWaitLockGuard (RTMutex& m)
    : mutex_ (m)
  {
    mutex_.wait_for_lock();
  }
  ~RTMutexWaitLockGuard()
  {
    mutex_.unlock();
  }

  // non-copyable
  RTMutexWaitLockGuard (const RTMutexWaitLockGuard&) = delete;
  RTMutexWaitLockGuard& operator= (const RTMutexWaitLockGuard&) = delete;

  // non-movable (same as std::lock_guard)
  RTMutexWaitLockGuard (RTMutexWaitLockGuard&&) = delete;
  RTMutexWaitLockGuard& operator= (RTMutexWaitLockGuard&&) = delete;
};

/*
 * do not use a std::mutex here because it may not be hard RT safe to
 * try_lock() / unlock() it (depending on how the mutex is implemented)
 */
bool
RTMutex::try_lock()
{
  return !locked_flag.test_and_set();
}

void
RTMutex::wait_for_lock()
{
  while (!try_lock())
    {
      // this doesn't happen very often and we are in a non-RT thread, so we
      // can block it for some time
      //  => wait for less than one frame drawing time until trying again
      float fps = 240;
      usleep (1000 * 1000 / fps);
    }
}

void
RTMutex::unlock()
{
  locked_flag.clear();
}

LV2Plugin::LV2Plugin (int rate, LV2_URID_Map *map, LV2_Worker_Schedule *schedule, LV2_Midnam *midnam) :
  schedule (schedule),
  midnam (midnam)
{
  synth.set_sample_rate (rate);

  midnam_model = LiquidSFZInternal::string_printf ("LiquidSFZ-%p\n", this);

  lv2_atom_forge_init (&forge, map);

  uris.midi_MidiEvent     = map->map (map->handle, LV2_MIDI__MidiEvent);
  uris.atom_Blank         = map->map (map->handle, LV2_ATOM__Blank);
  uris.atom_Object        = map->map (map->handle, LV2_ATOM__Object);
  uris.atom_URID          = map->map (map->handle, LV2_ATOM__URID);
  uris.atom_Path          = map->map (map->handle, LV2_ATOM__Path);
  uris.atom_Int           = map->map (map->handle, LV2_ATOM__Int);
  uris.patch_Get          = map->map (map->handle, LV2_PATCH__Get);
  uris.patch_Set          = map->map (map->handle, LV2_PATCH__Set);
  uris.patch_property     = map->map (map->handle, LV2_PATCH__property);
  uris.patch_value        = map->map (map->handle, LV2_PATCH__value);
  uris.state_StateChanged = map->map (map->handle, LV2_STATE__StateChanged);

  uris.liquidsfz_sfzfile  = map->map (map->handle, LIQUIDSFZ_URI "#sfzfile");
  uris.liquidsfz_program  = map->map (map->handle, LIQUIDSFZ_URI "#program");
}

void
LV2Plugin::write_state_changed()
{
  LV2_Atom_Forge_Frame frame;

  lv2_atom_forge_frame_time (&forge, 0);
  lv2_atom_forge_object (&forge, &frame, 0, uris.state_StateChanged);
  lv2_atom_forge_pop (&forge, &frame);
}

char *
LV2Plugin::get_midnam_str()
{
  std::lock_guard lg (midnam_str_mutex);
  return strdup (midnam_str.c_str());
}
char *
LV2Plugin::get_midnam_model()
{
  return strdup (midnam_model.c_str());
}

void
LV2Plugin::work (LV2_Worker_Respond_Function respond,
                 LV2_Worker_Respond_Handle   handle,
                 uint32_t                    size,
                 const void                 *data)
{
  if (size == sizeof (int) && *(int *) data == command_load)
    {
      rt_mutex.wait_for_lock();
      string filename = current_filename;
      int    program  = current_program;
      std::filesystem::path fp = filename;
      rt_mutex.unlock();

      auto set_status = [this] (const string& status)
        {
          RTMutexWaitLockGuard lg (rt_mutex);

          current_status = status;
          ui_redraw_required = true;
        };
      auto update_programs = [this] ()
        {
          RTMutexWaitLockGuard lg (rt_mutex);

          current_programs.clear();
          for (auto& program : synth.list_programs())
            current_programs.push_back (program.label());
          ui_redraw_required = true;
        };
      auto clear_programs = [this] ()
        {
          RTMutexWaitLockGuard lg (rt_mutex);

          current_programs.clear();
          ui_redraw_required = true;
        };
      synth.set_progress_function ([this] (double percent)
        {
          load_progress = percent;
        });
      if (synth.is_bank (filename))
        {
          if (synth.load_bank (filename))
            {
              set_status (string_printf ("loading '%s', program %d...", fp.filename().c_str(), program + 1));
              update_programs();

              if (synth.select_program (program))
                set_status ("OK");
              else
                set_status (string_printf ("ERROR loading '%s', program %d.", fp.filename().c_str(), program + 1));
            }
          else
            {
              set_status (string_printf ("ERROR loading bank '%s'.", fp.filename().c_str()));
              clear_programs();
            }
        }
      else
        {
          set_status (string_printf ("loading '%s'...", fp.filename().c_str()));
          clear_programs();

          if (synth.load (filename))
            set_status ("OK");
          else
            set_status (string_printf ("ERROR loading '%s'.", fp.filename().c_str()));
        }
      load_progress = -1;

      { // midnam string is accessed from other threads than the worker thread
        std::lock_guard lg (midnam_str_mutex);
        midnam_str = LiquidSFZInternal::gen_midnam (synth, midnam_model);
      }

      respond (handle, 1, "");
    }
}

void
LV2Plugin::work_response (uint32_t size, const void *data)
{
  load_in_progress = false;
  inform_ui        = true; // send notification to UI that current filename has changed
}

float
LV2Plugin::db_to_factor (float db)
{
  if (db <= -80)
    return 0;
  else
    return powf (10, db * 0.05f);
}

void
LV2Plugin::activate()
{
  synth.all_sound_off();
}

void
LV2Plugin::run (uint32_t n_samples)
{
  LV2_Atom_Forge_Frame notify_frame;

  if (old_level != *level)
    {
      synth.set_gain (db_to_factor (*level));
      old_level = *level;
    }
  if (old_freewheel != *freewheel)
    {
      /* set live mode to true if freewheel is disabled */
      synth.set_live_mode (*freewheel < 0.5f);
      old_freewheel = *freewheel;
    }

  const uint32_t capacity = notify_port->atom.size;
  lv2_atom_forge_set_buffer (&forge, (uint8_t *) notify_port, capacity);
  lv2_atom_forge_sequence_head (&forge, &notify_frame, 0);

  LV2_ATOM_SEQUENCE_FOREACH (midi_in, ev)
    {
      if (ev->body.type == uris.midi_MidiEvent && !load_in_progress)
        {
          const uint8_t *msg = (const uint8_t*)(ev + 1);

          uint time = ev->time.frames;
          int channel = msg[0] & 0x0f;
          switch (msg[0] & 0xf0)
            {
              case 0x90: synth.add_event_note_on (time, channel, msg[1], msg[2]);
                         break;
              case 0x80: synth.add_event_note_off (time, channel, msg[1]);
                         break;
              case 0xb0: synth.add_event_cc (time, channel, msg[1], msg[2]);
                         break;
              case 0xe0: synth.add_event_pitch_bend (time, channel, msg[1] + 128 * msg[2]);
                         break;
            }
          debug ("got midi event\n");
        }
    }
  float *outputs[2] = { left_out, right_out };
  if (!load_in_progress)
    {
      synth.process (outputs, n_samples);
    }
  else
    {
      for (uint i = 0; i < n_samples; i++)
        left_out[i] = right_out[i] = 0;
    }

  if (rt_mutex.try_lock())
    {
      if (!load_in_progress && file_or_program_changed)
        {
          load_in_progress = true;
          file_or_program_changed = false;

          schedule->schedule_work (schedule->handle, sizeof (int), &command_load);
        }
      rt_mutex.unlock();
    }
  if (inform_ui)
    {
      inform_ui = false;

      write_state_changed();

      if (midnam)
        midnam->update (midnam->handle);
    }

  // Close off sequence
  lv2_atom_forge_pop (&forge, &notify_frame);
}
void
LV2Plugin::connect_port (uint32_t   port,
              void*      data)
{
  switch ((PortIndex)port)
    {
      case MIDI_IN:    midi_in = (const LV2_Atom_Sequence *)data;
                       break;
      case LEFT_OUT:   left_out = (float*)data;
                       break;
      case RIGHT_OUT:  right_out = (float*)data;
                       break;
      case LEVEL:      level = (float *)data;
                       break;
      case FREEWHEEL:  freewheel = (float *)data;
                       break;
      case NOTIFY:     notify_port = (LV2_Atom_Sequence*)data;
                       break;
    }
}

LV2_State_Status
LV2Plugin::save (LV2_State_Store_Function store,
                 LV2_State_Handle         handle,
                 const LV2_Feature* const* features)
{
  rt_mutex.wait_for_lock();
  int    program  = current_program;
  string filename = current_filename;
  rt_mutex.unlock();

  if (filename.empty())
    {
      debug ("save: error: current filename is empty\n");
      return LV2_STATE_ERR_NO_PROPERTY;
    }

  LV2_State_Map_Path *map_path = nullptr;
#ifdef LV2_STATE__freePath
  LV2_State_Free_Path *free_path = nullptr;
#endif
  for (int i = 0; features[i]; i++)
    {
      if (!strcmp (features[i]->URI, LV2_STATE__mapPath))
        {
          map_path = (LV2_State_Map_Path *)features[i]->data;
        }
#ifdef LV2_STATE__freePath
      else if (!strcmp (features[i]->URI, LV2_STATE__freePath))
        {
          free_path = (LV2_State_Free_Path *)features[i]->data;
        }
#endif
    }

  string path = filename;
  if (map_path)
    {
      char *abstract_path = map_path->abstract_path (map_path->handle, path.c_str());
      if (!abstract_path)
        {
          debug ("save: map_path returned NULL for path '%s'", path.c_str());
          return LV2_STATE_ERR_UNKNOWN;
        }
      path = abstract_path;

#ifdef LV2_STATE__freePath
      if (free_path)
        {
          free_path->free_path (free_path->handle, abstract_path);
        }
      else
#endif
        {
          free (abstract_path);
        }
    }

  store (handle, uris.liquidsfz_sfzfile,
         path.c_str(), path.size() + 1,
         uris.atom_Path,
         LV2_STATE_IS_POD);

  store (handle, uris.liquidsfz_program,
         &program,
         sizeof (int32_t),
         uris.atom_Int,
         LV2_STATE_IS_POD);

  return LV2_STATE_SUCCESS;
}

LV2_State_Status
LV2Plugin::restore (LV2_State_Retrieve_Function retrieve,
                    LV2_State_Handle            handle,
                    const LV2_Feature* const*   features)
{
  LV2_State_Map_Path *map_path = nullptr;
#ifdef LV2_STATE__freePath
  LV2_State_Free_Path *free_path = nullptr;
#endif
  for (int i = 0; features[i]; i++)
    {
      if (!strcmp (features[i]->URI, LV2_STATE__mapPath))
        {
          map_path = (LV2_State_Map_Path *)features[i]->data;
        }
#ifdef LV2_STATE__freePath
      else if (!strcmp (features[i]->URI, LV2_STATE__freePath))
        {
          free_path = (LV2_State_Free_Path *)features[i]->data;
        }
#endif
    }
  if (!map_path)
    return LV2_STATE_ERR_NO_FEATURE;

  size_t      size;
  uint32_t    type;
  uint32_t    valflags;
  const void *value;

  int program = 0;
  value = retrieve (handle, uris.liquidsfz_program, &size, &type, &valflags);
  if (value && size == sizeof (int) && type == uris.atom_Int)
    program = *(int *) value;

  value = retrieve (handle, uris.liquidsfz_sfzfile, &size, &type, &valflags);
  if (value)
    {
      char *absolute_path = map_path->absolute_path (map_path->handle, (const char *)value);
      if (!absolute_path)
        {
          debug ("load: map_path returned NULL for path '%s'", (const char *)value);
          return LV2_STATE_ERR_UNKNOWN;
        }

#if LIQUIDSFZ_OS_WINDOWS
      char *real_path = strdup (absolute_path);
#else
      /* resolve symlinks so that sample paths relative to the .sfz file can be loaded */
      char *real_path = realpath (absolute_path, nullptr);
#endif
      if (real_path)
        {
          load_threadsafe (real_path, program);
          free (real_path);
        }
#ifdef LV2_STATE__freePath
      if (free_path)
        {
          free_path->free_path (free_path->handle, absolute_path);
        }
      else
#endif
        {
          free (absolute_path);
        }
    }

  return LV2_STATE_SUCCESS;
}

void
LV2Plugin::load_threadsafe (const string& filename, uint program)
{
  RTMutexWaitLockGuard lg (rt_mutex);

  current_filename = filename;
  current_program = program;
  file_or_program_changed = true;
}

float
LV2Plugin::load_progress_threadsafe() const
{
  return load_progress.load();
}

bool
LV2Plugin::redraw_required()
{
  RTMutexWaitLockGuard lg (rt_mutex);

  auto result = ui_redraw_required;
  ui_redraw_required = false;
  return result;
}

vector<string>
LV2Plugin::programs()
{
  RTMutexWaitLockGuard lg (rt_mutex);

  return current_programs;
}

int
LV2Plugin::program()
{
  RTMutexWaitLockGuard lg (rt_mutex);

  return current_program;
}

string
LV2Plugin::filename()
{
  RTMutexWaitLockGuard lg (rt_mutex);

  return current_filename;
}

string
LV2Plugin::status()
{
  RTMutexWaitLockGuard lg (rt_mutex);

  return current_status;
}

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
  LV2_Worker_Schedule *schedule = nullptr;
  LV2_URID_Map *map = nullptr;
  LV2_Midnam *midnam = nullptr;

  for (int i = 0; features[i]; i++)
    {
      if (!strcmp (features[i]->URI, LV2_URID__map))
        {
          map = (LV2_URID_Map*)features[i]->data;
        }
      else if (!strcmp (features[i]->URI, LV2_WORKER__schedule))
        {
          schedule = (LV2_Worker_Schedule*)features[i]->data;
        }
      else if (!strcmp (features[i]->URI, LV2_MIDNAM__update))
        {
          midnam = (LV2_Midnam*)features[i]->data;
        }
    }
  if (!map || !schedule)
    return nullptr; // host bug, we need this feature

  return new LV2Plugin (rate, map, schedule, midnam);
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
  LV2Plugin* self = (LV2Plugin*)instance;
  self->connect_port (port, data);
}

static void
activate (LV2_Handle instance)
{
  LV2Plugin *self = static_cast<LV2Plugin *> (instance);
  self->activate();
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
  LV2Plugin *self = static_cast<LV2Plugin *> (instance);
  self->run (n_samples);
}

static void
deactivate (LV2_Handle instance)
{
}

static void
cleanup (LV2_Handle instance)
{
  LV2Plugin *self = static_cast<LV2Plugin *> (instance);
  delete self;
}

static LV2_State_Status
save (LV2_Handle                instance,
      LV2_State_Store_Function  store,
      LV2_State_Handle          handle,
      uint32_t                  flags,
      const LV2_Feature* const* features)
{
  LV2Plugin *self = (LV2Plugin *) instance;

  return self->save (store, handle, features);
}

static LV2_State_Status
restore (LV2_Handle                  instance,
         LV2_State_Retrieve_Function retrieve,
         LV2_State_Handle            handle,
         uint32_t                    flags,
         const LV2_Feature* const*   features)
{
  LV2Plugin *self = (LV2Plugin *) instance;

  return self->restore (retrieve, handle, features);
}

static LV2_Worker_Status
work (LV2_Handle                  instance,
      LV2_Worker_Respond_Function respond,
      LV2_Worker_Respond_Handle   handle,
      uint32_t                    size,
      const void*                 data)
{
  LV2Plugin* self = (LV2Plugin*) instance;

  self->work (respond, handle, size, data);

  return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response (LV2_Handle  instance,
               uint32_t    size,
               const void* data)
{
  LV2Plugin* self = (LV2Plugin*) instance;

  self->work_response (size, data);

  return LV2_WORKER_SUCCESS;
}

static char*
mn_file (LV2_Handle instance)
{
  LV2Plugin* self = (LV2Plugin*) instance;
  return self->get_midnam_str();
}

static char*
mn_model (LV2_Handle instance)
{
  LV2Plugin* self = (LV2Plugin*) instance;
  return self->get_midnam_model();
}

static void
mn_free (char *v)
{
  free (v);
}

static const void*
extension_data (const char* uri)
{
  if (!strcmp (uri, LV2_STATE__interface))
    {
      static const LV2_State_Interface state = { save, restore };
      return &state;
    }
  if (!strcmp (uri, LV2_WORKER__interface))
    {
      static const LV2_Worker_Interface worker = { work, work_response, nullptr };
      return &worker;
    }
  if (!strcmp (uri, LV2_MIDNAM__interface))
    {
      static const LV2_Midnam_Interface midnam = { mn_file, mn_model, mn_free };
      return &midnam;
    }
  return nullptr;
}

static const LV2_Descriptor descriptor = {
  LIQUIDSFZ_URI,
  instantiate,
  connect_port,
  activate,
  run,
  deactivate,
  cleanup,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
  switch (index)
    {
      case 0:  return &descriptor;
      default: return NULL;
    }
}
