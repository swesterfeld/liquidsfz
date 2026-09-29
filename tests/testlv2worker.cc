// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "../lv2/lv2plugin.hh"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" const LV2_Descriptor *lv2_descriptor (uint32_t index);

struct Host
{
  std::map<std::string, LV2_URID> urids;
  LV2_URID_Map map { this, map_uri };
  LV2_Worker_Schedule schedule { this, schedule_work };
  std::unique_ptr<LV2Plugin> plugin;
  bool synchronous = false;
  bool pending_work = false;
  bool pending_response = false;
  int command = 0;
  int schedules = 0;
  int loads = 0;
  const LV2_Worker_Interface *worker = static_cast<const LV2_Worker_Interface *>
    (lv2_descriptor (0)->extension_data (LV2_WORKER__interface));
  float level = 0;
  float freewheel = 0;
  std::array<float, 64> left {}, right {};
  alignas(8) std::array<uint8_t, 256> midi {};
  alignas(8) std::array<uint8_t, 4096> notify {};

  static LV2_URID
  map_uri (LV2_URID_Map_Handle handle, const char *uri)
  {
    auto& urids = static_cast<Host *> (handle)->urids;
    auto result = urids.emplace (uri, urids.size() + 1);
    return result.first->second;
  }

  static LV2_Worker_Status
  schedule_work (LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
  {
    auto& host = *static_cast<Host *> (handle);
    host.schedules++;
    assert (!host.pending_work && !host.pending_response);
    assert (size == sizeof (host.command));
    memcpy (&host.command, data, size);
    if (host.synchronous)
      host.do_work();
    else
      host.pending_work = true;
    return LV2_WORKER_SUCCESS;
  }

  static LV2_Worker_Status
  respond (LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
  {
    auto& host = *static_cast<Host *> (handle);
    assert (size == 1 && *static_cast<const char *> (data) == 0);
    if (host.synchronous)
      host.worker->work_response (host.plugin.get(), size, data);
    else
      host.pending_response = true;
    return LV2_WORKER_SUCCESS;
  }

  void
  do_work()
  {
    loads++;
    assert (worker->work (plugin.get(), respond, this, sizeof (command), &command) == LV2_WORKER_SUCCESS);
  }

  Host()
  {
    plugin = std::make_unique<LV2Plugin> (48000, &map, &schedule, nullptr, nullptr);
    plugin->connect_port (LV2Plugin::MIDI_IN, midi.data());
    plugin->connect_port (LV2Plugin::NOTIFY, notify.data());
    plugin->connect_port (LV2Plugin::LEFT_OUT, left.data());
    plugin->connect_port (LV2Plugin::RIGHT_OUT, right.data());
    plugin->connect_port (LV2Plugin::LEVEL, &level);
    plugin->connect_port (LV2Plugin::FREEWHEEL, &freewheel);
    // Pre-map these before rendering.
    map_uri (this, LV2_ATOM__Sequence);
    map_uri (this, LV2_MIDI__MidiEvent);
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
        auto *msg = reinterpret_cast<uint8_t *> (event + 1);
        msg[0] = 0x90;
        msg[1] = 60;
        msg[2] = 100;
        sequence->atom.size += sizeof (*event) + 8;
      }
    reinterpret_cast<LV2_Atom_Sequence *> (notify.data())->atom.size = notify.size();
    plugin->run (left.size());
  }

  bool
  audible() const
  {
    for (auto value : left)
      if (value != 0)
        return true;
    return false;
  }

  bool
  notified() const
  {
    return reinterpret_cast<const LV2_Atom_Sequence *> (notify.data())->atom.size > sizeof (LV2_Atom_Sequence_Body);
  }

  void
  finish_work()
  {
    assert (pending_work);
    pending_work = false;
    std::thread thread ([this] { do_work(); });
    thread.join();
  }
  void
  deliver_response()
  {
    assert (pending_response);
    pending_response = false;
    worker->work_response (plugin.get(), 1, "");
  }
};

int
main()
{
  // In particular, the original synchronous-worker deadlock must time out.
  alarm (20);
  char temp[] = "/tmp/liquidsfz-worker-XXXXXX";
  const char *directory = mkdtemp (temp);
  assert (directory);
  const std::string sine = std::string (directory) + "/sine.sfz";
  const std::string silence = std::string (directory) + "/silence.sfz";
  std::ofstream (sine) << "<region> sample=*sine\n";
  std::ofstream (silence) << "<region> sample=*silence\n";

  // Immediate completion during freewheeling: no lock recursion.
  {
    Host host;
    host.synchronous = true;
    host.freewheel = 1;
    host.plugin->load_threadsafe (sine, 0);
    host.run();
    assert (host.loads == 1 && host.notified());
    host.run (true);
    assert (host.audible() && host.schedules == 1);
  }

  // Asynchronous work stays silent until its response, and serializes a newer
  // request arriving while the first load is in progress.
  {
    Host host;
    host.plugin->load_threadsafe (silence, 0);
    host.run();
    host.finish_work();
    host.plugin->load_threadsafe (sine, 0);
    host.run (true);
    assert (!host.audible() && host.schedules == 1 && !host.notified());
    host.deliver_response();
    host.run();
    assert (host.schedules == 2 && host.notified());
    host.run();
    assert (!host.audible() && host.schedules == 2);
    host.finish_work();
    host.run (true);
    assert (!host.audible() && !host.notified());
    host.deliver_response();
    host.run (true);
    assert (host.audible() && host.notified() && host.schedules == 2);
  }

  unlink (sine.c_str());
  unlink (silence.c_str());
  rmdir (directory);
}
