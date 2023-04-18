// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "liquidsfz.hh"
#include "midnam.hh"
#include "log.hh"

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

#include "lv2_midnam.h"

#include <string>
#include <atomic>
#include <mutex>

#include <math.h>

#define LIQUIDSFZ_URI      "http://spectmorph.org/plugins/liquidsfz"

#ifndef LV2_STATE__StateChanged
#define LV2_STATE__StateChanged LV2_STATE_PREFIX "StateChanged"
#endif

using namespace LiquidSFZ;

using std::string;

namespace
{

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
      va_end (ap);
    }
}
#else
void
debug (const char *format, ...)
{
}
#endif

class LV2Plugin
{
  enum PortIndex
  {
    MIDI_IN    = 0,
    LEFT_OUT   = 1,
    RIGHT_OUT  = 2,
    LEVEL      = 3,
    FREEWHEEL  = 4,
    NOTIFY     = 5
  };

  // URIs
  struct URIS
  {
    LV2_URID atom_Blank;
    LV2_URID atom_Object;
    LV2_URID atom_URID;
    LV2_URID atom_Path;
    LV2_URID midi_MidiEvent;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_value;
    LV2_URID state_StateChanged;

    LV2_URID liquidsfz_sfzfile;
  } uris;

  // Port buffers
  const LV2_Atom_Sequence *midi_in = nullptr;
  float                   *left_out = nullptr;
  float                   *right_out = nullptr;
  const float             *level = nullptr;
  const float             *freewheel = nullptr;
  LV2_Atom_Sequence       *notify_port = nullptr;

  string                   queue_filename;
  string                   current_filename;
  string                   load_filename;
  bool                     load_in_progress = false;
  bool                     inform_ui = false;
  static constexpr int     command_load = 0x10001234; // just some random number
  float                    old_level = 1000;          // outside range [-80:20]
  float                    old_freewheel = -1;        // outside boolean range [0:1]

  LV2_Worker_Schedule     *schedule = nullptr;
  LV2_Midnam              *midnam = nullptr;
  LV2_Atom_Forge           forge;
  Synth                    synth;
  string                   midnam_model;
  string                   midnam_str;
  std::mutex               midnam_str_mutex;
public:

  LV2Plugin (int rate, LV2_URID_Map *map, LV2_Worker_Schedule *schedule, LV2_Midnam *midnam) :
    schedule (schedule),
    midnam (midnam)
  {
    /* avoid allocations in RT thread */
    load_filename.reserve (1024);
    queue_filename.reserve (1024);
    current_filename.reserve (1024);

    synth.set_sample_rate (rate);

    midnam_model = LiquidSFZInternal::string_printf ("LiquidSFZ-%p\n", this);

    lv2_atom_forge_init (&forge, map);

    uris.midi_MidiEvent     = map->map (map->handle, LV2_MIDI__MidiEvent);
    uris.atom_Blank         = map->map (map->handle, LV2_ATOM__Blank);
    uris.atom_Object        = map->map (map->handle, LV2_ATOM__Object);
    uris.atom_URID          = map->map (map->handle, LV2_ATOM__URID);
    uris.atom_Path          = map->map (map->handle, LV2_ATOM__Path);
    uris.patch_Get          = map->map (map->handle, LV2_PATCH__Get);
    uris.patch_Set          = map->map (map->handle, LV2_PATCH__Set);
    uris.patch_property     = map->map (map->handle, LV2_PATCH__property);
    uris.patch_value        = map->map (map->handle, LV2_PATCH__value);
    uris.state_StateChanged = map->map (map->handle, LV2_STATE__StateChanged);

    uris.liquidsfz_sfzfile  = map->map (map->handle, LIQUIDSFZ_URI "#sfzfile");
  }

  const char *
  read_set_filename (const LV2_Atom_Object *obj)
  {
    if (obj->body.otype != uris.patch_Set)
      return nullptr;

    const LV2_Atom *property = nullptr;
    lv2_atom_object_get (obj, uris.patch_property, &property, 0);
    if (!property || property->type != uris.atom_URID)
      return nullptr;

    if (((const LV2_Atom_URID *)property)->body != uris.liquidsfz_sfzfile)
      return nullptr;

    const LV2_Atom *file_path = nullptr;
    lv2_atom_object_get (obj, uris.patch_value, &file_path, 0);
    if (!file_path || file_path->type != uris.atom_Path)
      return nullptr;

    const char *filename = (const char *) (file_path + 1);
    return filename;
  }

  void
  write_set_filename()
  {
    LV2_Atom_Forge_Frame frame;

    lv2_atom_forge_frame_time (&forge, 0);
    lv2_atom_forge_object (&forge, &frame, 0, uris.patch_Set);
    lv2_atom_forge_property_head (&forge, uris.patch_property, 0);
    lv2_atom_forge_urid (&forge, uris.liquidsfz_sfzfile);
    lv2_atom_forge_property_head (&forge, uris.patch_value, 0);
    lv2_atom_forge_path (&forge, current_filename.c_str(), current_filename.length());
    lv2_atom_forge_pop (&forge, &frame);
  }

  void
  write_state_changed()
  {
    LV2_Atom_Forge_Frame frame;

    lv2_atom_forge_frame_time (&forge, 0);
    lv2_atom_forge_object (&forge, &frame, 0, uris.state_StateChanged);
    lv2_atom_forge_pop (&forge, &frame);
  }

  char *
  get_midnam_str()
  {
    std::lock_guard lg (midnam_str_mutex);
    return strdup (midnam_str.c_str());
  }
  char *
  get_midnam_model()
  {
    return strdup (midnam_model.c_str());
  }
  void
  work (LV2_Worker_Respond_Function respond,
        LV2_Worker_Respond_Handle   handle,
        uint32_t                    size,
        const void                 *data)
  {
    if (size == sizeof (int) && *(int *) data == command_load)
      {
        debug ("loading file %s\n", load_filename.c_str());

        synth.load (load_filename);

        { // midnam string is accessed from other threads than the worker thread
          std::lock_guard lg (midnam_str_mutex);
          midnam_str = LiquidSFZInternal::gen_midnam (synth, midnam_model);
        }

        respond (handle, 1, "");
      }
  }

  void
  work_response (uint32_t size, const void *data)
  {
    load_in_progress = false;
    current_filename = load_filename;
    inform_ui        = true; // send notification to UI that current filename has changed
  }

  float
  db_to_factor (float db)
  {
    if (db <= -80)
      return 0;
    else
      return powf (10, db * 0.05f);
  }

  void
  activate()
  {
    synth.all_sound_off();
  }

  void
  run (uint32_t n_samples)
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
        if (ev->body.type == uris.atom_Blank || ev->body.type == uris.atom_Object)
          {
            const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;

            if (obj->body.otype == uris.patch_Get)
              {
                debug ("got patch get message\n");

                write_set_filename();
              }
            else if (obj->body.otype == uris.patch_Set && !load_in_progress)
              {
                if (const char *filename = read_set_filename (obj))
                  {
                    size_t len = strlen (filename);
                    if (len < queue_filename.capacity()) // avoid allocations in RT context
                      {
                        queue_filename = filename;
                      }
                  }
              }
          }
        else if (ev->body.type == uris.midi_MidiEvent && !load_in_progress)
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

    if (!load_in_progress && !queue_filename.empty())
      {
        load_in_progress = true;
        load_filename    = queue_filename;
        queue_filename   = "";

        schedule->schedule_work (schedule->handle, sizeof (int), &command_load);
      }
    if (inform_ui)
      {
        inform_ui = false;

        write_state_changed();
        write_set_filename();

        if (midnam)
          midnam->update (midnam->handle);
      }

    // Close off sequence
    lv2_atom_forge_pop (&forge, &notify_frame);
  }
  void
  connect_port (uint32_t   port,
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
  save (LV2_State_Store_Function store,
        LV2_State_Handle         handle,
        const LV2_Feature* const* features)
  {
    LV2_State_Map_Path *map_path = nullptr;
    for (int i = 0; features[i]; i++)
      {
        if (!strcmp (features[i]->URI, LV2_STATE__mapPath))
          {
            map_path = (LV2_State_Map_Path *)features[i]->data;
          }
      }

    string path = current_filename;
    if (map_path)
      {
        char *abstract_path = map_path->abstract_path (map_path->handle, path.c_str());
        if (!abstract_path)
          {
            debug ("save: map_path returned NULL for path '%s'", path.c_str());
            return LV2_STATE_ERR_UNKNOWN;
          }
        path = abstract_path;
        free (abstract_path);
      }

    store (handle, uris.liquidsfz_sfzfile,
           path.c_str(), path.size() + 1,
           uris.atom_Path,
           LV2_STATE_IS_POD);

    return LV2_STATE_SUCCESS;
  }

  LV2_State_Status
  restore (LV2_State_Retrieve_Function retrieve,
           LV2_State_Handle            handle,
           const LV2_Feature* const*   features)
  {
    LV2_State_Map_Path *map_path = nullptr;
    for (int i = 0; features[i]; i++)
      {
        if (!strcmp (features[i]->URI, LV2_STATE__mapPath))
          {
            map_path = (LV2_State_Map_Path *)features[i]->data;
          }
      }
    if (!map_path)
      return LV2_STATE_ERR_NO_FEATURE;

    size_t      size;
    uint32_t    type;
    uint32_t    valflags;
    const void *value = retrieve (handle, uris.liquidsfz_sfzfile, &size, &type, &valflags);
    if (value)
      {
        char *absolute_path = map_path->absolute_path (map_path->handle, (const char *)value);

        if (absolute_path)
          {
            /* resolve symlinks so that sample paths relative to the .sfz file can be loaded */
            char *real_path = realpath (absolute_path, nullptr);
            if (real_path)
              {
                queue_filename = real_path;
                free (real_path);
              }
          }
        else
          {
            debug ("load: map_path returned NULL for path '%s'", (const char *)value);
            return LV2_STATE_ERR_UNKNOWN;
          }

        free (absolute_path);
      }

    return LV2_STATE_SUCCESS;
  }
};

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
