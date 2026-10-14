// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#if __has_include (<lv2/core/lv2.h>)
#include <lv2/core/lv2.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#else
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <unistd.h>

static void
require (bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error (message);
}

struct Host
{
  void *library = nullptr;
  const LV2_Descriptor *descriptor = nullptr;
  LV2_Handle instance = nullptr;
  const LV2_Worker_Interface *worker = nullptr;
  bool activated = false;
  bool worker_ok = true;
  unsigned work_count = 0;
  std::string sfz;
  std::map<std::string, LV2_URID> urids;
  LV2_URID_Map map { this, map_uri };
  LV2_Worker_Schedule schedule { this, schedule_work };
  float level = 0, freewheel = 1;
  std::array<float, 64> left {}, right {};
  alignas(8) std::array<uint8_t, 256> midi {};
  alignas(8) std::array<uint8_t, 4096> notify {};

  ~Host()
  {
    if (activated && descriptor->deactivate)
      descriptor->deactivate (instance);
    if (instance)
      descriptor->cleanup (instance);
    if (library)
      dlclose (library);
  }

  static LV2_URID
  map_uri (LV2_URID_Map_Handle handle, const char *uri)
  {
    auto& urids = static_cast<Host *> (handle)->urids;
    return urids.emplace (uri, urids.size() + 1).first->second;
  }

  static LV2_Worker_Status
  respond (LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
  {
    auto& host = *static_cast<Host *> (handle);
    const auto status = host.worker->work_response (host.instance, size, data);
    host.worker_ok &= status == LV2_WORKER_SUCCESS;
    return status;
  }

  static LV2_Worker_Status
  schedule_work (LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
  {
    // Synchronous work is supported in freewheeling mode.
    auto& host = *static_cast<Host *> (handle);
    host.work_count++;
    const auto status = host.worker->work (host.instance, respond, handle, size, data);
    host.worker_ok &= status == LV2_WORKER_SUCCESS;
    return status;
  }

  static char *
  map_path (LV2_State_Map_Path_Handle, const char *path)
  {
    return strdup (path);
  }

  static const void *
  retrieve (LV2_State_Handle handle, uint32_t key, size_t *size, uint32_t *type, uint32_t *flags)
  {
    auto& host = *static_cast<Host *> (handle);
    if (key != map_uri (&host, "http://spectmorph.org/plugins/liquidsfz#sfzfile"))
      return nullptr;
    *size = host.sfz.size() + 1;
    *type = map_uri (&host, LV2_ATOM__Path);
    *flags = LV2_STATE_IS_POD;
    return host.sfz.c_str();
  }

  void
  open (const char *plugin_path, const char *sfz_path)
  {
    const auto plugin = std::filesystem::canonical (plugin_path);
    sfz = std::filesystem::canonical (sfz_path).string();
    library = dlopen (plugin.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library)
      throw std::runtime_error (dlerror());
    auto get_descriptor = reinterpret_cast<LV2_Descriptor_Function> (dlsym (library, "lv2_descriptor"));
    require (get_descriptor, "Plugin does not export lv2_descriptor");
    descriptor = get_descriptor (0);
    require (descriptor && descriptor->extension_data, "Missing LV2 descriptor/extensions");
    worker = static_cast<const LV2_Worker_Interface *> (descriptor->extension_data (LV2_WORKER__interface));
    const auto *state = static_cast<const LV2_State_Interface *> (descriptor->extension_data (LV2_STATE__interface));
    require (worker && worker->work && worker->work_response && state && state->restore,
             "Missing LV2 worker/state interface");

    LV2_Feature map_feature { LV2_URID__map, &map };
    LV2_Feature worker_feature { LV2_WORKER__schedule, &schedule };
    const LV2_Feature *features[] = { &map_feature, &worker_feature, nullptr };
    const std::string bundle = plugin.parent_path().string() + "/";
    instance = descriptor->instantiate (descriptor, 48000, bundle.c_str(), features);
    require (instance, "LV2 instantiation failed");

    // Port indices are part of the published liquidsfz LV2 interface.
    descriptor->connect_port (instance, 0, midi.data());
    descriptor->connect_port (instance, 1, left.data());
    descriptor->connect_port (instance, 2, right.data());
    descriptor->connect_port (instance, 3, &level);
    descriptor->connect_port (instance, 4, &freewheel);
    descriptor->connect_port (instance, 5, notify.data());

    LV2_State_Map_Path paths { this, map_path, map_path };
    LV2_Feature path_feature { LV2_STATE__mapPath, &paths };
    const LV2_Feature *state_features[] = { &path_feature, nullptr };
    require (state->restore (instance, retrieve, this, 0, state_features) == LV2_STATE_SUCCESS,
             "Could not restore the FLAC instrument");
    if (descriptor->activate)
      descriptor->activate (instance);
    activated = true;
  }

  void
  run (bool note = false)
  {
    auto *sequence = reinterpret_cast<LV2_Atom_Sequence *> (midi.data());
    sequence->atom.type = map_uri (this, LV2_ATOM__Sequence);
    sequence->atom.size = sizeof (sequence->body);
    sequence->body = {};
    if (note)
      {
        auto *event = reinterpret_cast<LV2_Atom_Event *> (sequence + 1);
        event->time.frames = 0;
        event->body.type = map_uri (this, LV2_MIDI__MidiEvent);
        event->body.size = 3;
        const uint8_t message[] = { 0x90, 60, 127 };
        memcpy (event + 1, message, sizeof (message));
        sequence->atom.size += sizeof (*event) + lv2_atom_pad_size (sizeof (message));
      }
    reinterpret_cast<LV2_Atom *> (notify.data())->size = notify.size();
    // Unwritten output must also fail the test.
    left.fill (NAN);
    right.fill (NAN);
    descriptor->run (instance, left.size());
    if (worker->end_run)
      require (worker->end_run (instance) == LV2_WORKER_SUCCESS, "LV2 end_run failed");
    require (worker_ok, "LV2 worker failed");
    for (size_t i = 0; i < left.size(); i++)
      require (std::isfinite (left[i]) && std::isfinite (right[i]), "Non-finite/unwritten plugin output");
  }
};

int
main (int argc, char **argv)
{
  if (argc != 3)
    {
      fprintf (stderr, "Usage: %s PLUGIN.so FLAC-INSTRUMENT.sfz\n", argv[0]);
      return 1;
    }
  alarm (30);
  try
    {
      Host host;
      host.open (argv[1], argv[2]);
      // First block completes the synchronous load; the second verifies silence.
      for (int block = 0; block < 2; block++)
        {
          host.run();
          for (size_t i = 0; i < host.left.size(); i++)
            require (host.left[i] == 0 && host.right[i] == 0, "Plugin emitted audio before note-on");
        }
      require (host.work_count == 1, "Instrument load was not scheduled exactly once");
      double left_energy = 0, right_energy = 0;
      for (int block = 0; block < 8; block++)
        {
          host.run (block == 0);
          for (size_t i = 0; i < host.left.size(); i++)
            {
              left_energy += host.left[i] * host.left[i];
              right_energy += host.right[i] * host.right[i];
            }
        }
      require (left_energy > 1e-6 && right_energy > 1e-6,
               "Plugin produced no FLAC audio on one or both channels; check bundled codec support");
      printf ("Installed LV2 FLAC output passed: left energy=%g, right energy=%g\n", left_energy, right_energy);
      return 0;
    }
  catch (const std::exception& error)
    {
      fprintf (stderr, "LV2 output smoke test failed: %s\n", error.what());
      return 1;
    }
}
