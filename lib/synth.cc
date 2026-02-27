// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "synth.hh"
#include "liquidsfz.hh"
#include "pugixml.hh"

#include <stdarg.h>

using LiquidSFZ::Log;

using std::string;
using std::vector;
using std::min;

namespace LiquidSFZInternal {

void
Synth::process_audio (float **outputs, uint n_frames, uint offset)
{
  uint i = 0;
  while (i < n_frames)
    {
      const uint todo = min (n_frames - i, MAX_BLOCK_SIZE);

      float *outputs_offset[] = {
        outputs[0] + offset + i,
        outputs[1] + offset + i
      };

      for (Voice *voice : active_voices_)
        voice->process (outputs_offset, todo);

      update_idle_voices();
      i += todo;
    }
  global_frame_count += n_frames;
}

void
Synth::sort_events_stable()
{
  // fast path: return if events are already sorted properly
  if (std::is_sorted (events.begin(), events.end(),
      [] (const Event& a, const Event& b)
        { return a.time_frames < b.time_frames; }
      ))
    return;

  /*
   * we don't want to use std::stable_sort, because it allocates memory
   *
   * std::sort doesn't allocate extra memory, but is not stable, so we use an
   * extra field to preserve the relative order of events with the same
   * timestamp
   */
  for (size_t i = 0; i < events.size(); i++)
    events[i].tmp_sort_index = i;

  auto event_cmp = [] (const Event& a, const Event& b)
    {
      if (a.time_frames == b.time_frames)
        return a.tmp_sort_index < b.tmp_sort_index;
      else
        return a.time_frames < b.time_frames;
    };
  std::sort (events.begin(), events.end(), event_cmp);
}

void
Synth::process (float **outputs, uint n_frames)
{
  zero_float_block (n_frames, outputs[0]);
  zero_float_block (n_frames, outputs[1]);

  /*
   * Our public API specifies that events must be added sorted by time stamp.
   * However if they are not sorted, we do it here, in order to avoid problems
   * in the event handling code below.
   */
  sort_events_stable();

  uint offset = 0;
  for (const auto& event : events)
    {
      // ensure that new offset from event is not larger than n_frames
      const uint new_offset = min <uint> (event.time_frames, n_frames);

      // process any audio that is before the event
      process_audio (outputs, new_offset - offset, offset);
      offset = new_offset;

      // process event at timestamp offset
      switch (event.type)
        {
          case Event::Type::NOTE_ON:    note_on (event.channel, event.arg1, event.arg2);
                                        break;
          case Event::Type::NOTE_OFF:   note_off (event.channel, event.arg1);
                                        break;
          case Event::Type::CC:         update_cc (event.channel, event.arg1, event.arg2);
                                        break;
          case Event::Type::PITCH_BEND: update_pitch_bend (event.channel, event.arg1);
                                        break;
          default:                      debug ("process: unsupported event type %d\n", int (event.type));
        }
    }
  events.clear();

  // process frames after last event
  process_audio (outputs, n_frames - offset, offset);
}

void
Synth::all_sound_off()
{
  for (auto& voice : voices_)
    voice.kill();

  update_idle_voices();
}

void
Synth::system_reset()
{
  all_sound_off();
  init_channels();
}

void
Synth::set_progress_function (std::function<void (double)> function)
{
  progress_function_ = function;
}

void
Synth::set_log_function (std::function<void (Log, const char *)> function)
{
  log_function_ = function;
}

void
Synth::set_log_level (Log log_level)
{
  log_level_ = log_level;
}

static const char *
log2str (Log level)
{
  switch (level)
    {
      case Log::DEBUG:    return "liquidsfz::debug";
      case Log::INFO:     return "liquidsfz::info";
      case Log::WARNING:  return "liquidsfz::warning";
      case Log::ERROR:    return "liquidsfz::error";
      default:            return "***loglevel?***";
    }
}

void
Synth::logv (Log level, const char *format, va_list vargs) const
{
  char buffer[1024];

  vsnprintf (buffer, sizeof (buffer), format, vargs);

  if (log_function_)
    log_function_ (level, buffer);
  else
    fprintf (stderr, "[%s] %s", log2str (level), buffer);
}

void
Synth::error (const char *format, ...) const
{
  if (log_level_ <= Log::ERROR)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::ERROR, format, ap);
      va_end (ap);
    }
}

void
Synth::warning (const char *format, ...) const
{
  if (log_level_ <= Log::WARNING)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::WARNING, format, ap);
      va_end (ap);
    }
}

void
Synth::info (const char *format, ...) const
{
  if (log_level_ <= Log::INFO)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::INFO, format, ap);
      va_end (ap);
    }
}

void
Synth::debug (const char *format, ...) const
{
  if (log_level_ <= Log::DEBUG)
    {
      va_list ap;

      va_start (ap, format);
      logv (Log::DEBUG, format, ap);
      va_end (ap);
    }
}

/* AriaBank handling */
bool
Synth::is_bank (const string& filename) const
{
  pugi::xml_document doc;
  auto result = doc.load_file (filename.c_str());
  if (!result)
    return false;

  auto bank = doc.child ("AriaBank");
  if (!bank)
    return false;

  auto program = bank.child ("AriaProgram");
  if (!program)
    return false;

  return true;
}

bool
Synth::load_bank (const string& filename)
{
  /* unload previous sfz / bank */
  bank_programs_.clear();
  bank_defines_.clear();
  unload();

  pugi::xml_document doc;
  auto result = doc.load_file (filename.c_str());
  if (!result)
    return false;

  auto bank = doc.child ("AriaBank");
  if (!bank)
    return false;

  vector<ProgramInfo> programs;
  int i = 0;
  for (pugi::xml_node program : bank.children ("AriaProgram"))
    {
      string name = program.attribute("name").as_string();

      auto elem = program.child ("AriaElement");
      string p = elem.attribute ("path").as_string();

      programs.push_back (ProgramInfo { i++, name, path_resolve_case_insensitive (path_absolute (path_join (path_dirname (filename), p))) });
    }
  vector<Control::Define> defines;
  for (pugi::xml_node define : bank.children ("Define"))
    {
      string name = define.attribute ("name").as_string();
      string value = define.attribute ("value").as_string();

      Control::Define def;
      def.variable = name;
      def.value = value;
      defines.push_back (def);
    }
  if (programs.size())
    {
      bank_programs_ = programs;
      bank_defines_ = defines;
      return true;
    }

  return false;
}

bool
Synth::select_program (uint program)
{
  if (program >= bank_programs_.size())
    {
      error ("invalid program %d", program);
      unload();
      return false;
    }
  else
    {
      return load_internal (bank_programs_[program].sfz_filename);
    }
}


std::mutex            Global::mutex_;
std::weak_ptr<Global> Global::global_;

}
