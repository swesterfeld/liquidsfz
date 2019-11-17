/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019  Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "liquidsfz.hh"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

#include <string>

#define LIQUIDSFZ_URI      "http://spectmorph.org/plugins/liquidsfz"

#ifndef LV2_STATE__StateChanged
#define LV2_STATE__StateChanged LV2_STATE_PREFIX "StateChanged"
#endif

using namespace LiquidSFZ;

namespace
{

class LV2Plugin
{
  enum PortIndex {
    MIDI_IN    = 0,
    LEFT_OUT   = 1,
    RIGHT_OUT  = 2,
    NOTIFY     = 3
  };

  // URIs
  struct {
    LV2_URID midi_MidiEvent;
  } uris;

  // Port buffers
  const LV2_Atom_Sequence *midi_in;
  float                   *left_out;
  float                   *right_out;
  LV2_Atom_Sequence       *notify_port;

  FILE *out;
  Synth synth;
public:
  LV2Plugin (int rate)
  {
    out = fopen ("/tmp/liquidsfz.log", "w");
    synth.set_sample_rate (rate);
    synth.load ("/home/stefan/sfz/Church Organ/Church Organ.sfz");
  }

  void
  init_map (LV2_URID_Map *map)
  {
    uris.midi_MidiEvent = map->map (map->handle, LV2_MIDI__MidiEvent);
  }

  void
  run (uint32_t n_samples)
  {
    LV2_ATOM_SEQUENCE_FOREACH (midi_in, ev)
      {
        if (ev->body.type == uris.midi_MidiEvent)
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
              }
            fprintf (out, "got event\n");
            fflush (out);
          }
      }
    float *outputs[2] = { left_out, right_out };
    synth.process (outputs, n_samples);
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
#if 0
        case SPECTMORPH_NOTIFY:     self->notify_port = (LV2_Atom_Sequence*)data;
                                    break;
#endif
      }
  }

};

}

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{

  LV2_URID_Map *map = nullptr;
  for (int i = 0; features[i]; i++)
    {
      if (!strcmp (features[i]->URI, LV2_URID__map))
        {
          map = (LV2_URID_Map*)features[i]->data;
        }
    }
  if (!map)
    return nullptr; // host bug, we need this feature

  auto self = new LV2Plugin (rate);

  self->init_map (map);

  return self;
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
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
  return LV2_STATE_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle            handle,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
  return LV2_STATE_SUCCESS;
}

static const void*
extension_data (const char* uri)
{
  static const LV2_State_Interface  state  = { save, restore };

  if (!strcmp(uri, LV2_STATE__interface))
    {
      return &state;
    }
  return NULL;
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
