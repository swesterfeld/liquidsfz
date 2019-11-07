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

#ifndef LIQUIDSFZ_LOADER_HH
#define LIQUIDSFZ_LOADER_HH

#include <string>
#include <filesystem>
#include <set>

#include "log.hh"
#include "samplecache.hh"

namespace LiquidSFZInternal
{

enum class Trigger {
  ATTACK,
  RELEASE,
  CC
};

enum class LoopMode {
  NONE, /* no_loop */
  ONE_SHOT, /* one_shot */
  CONTINUOUS, /* loop_continuous */
  SUSTAIN, /* loop_sustain */
  DEFAULT /* automatically use NONE or CONTINUOUS (from sample) */
};

struct Region
{
  std::string sample;
  SampleCache::Entry *cached_sample = nullptr;

  int lokey = 0;
  int hikey = 127;
  int lovel = 0;
  int hivel = 127;
  double lorand = 0;
  double hirand = 1;
  int pitch_keycenter = 60;
  int loop_start = 0;
  int loop_end = 0;
  LoopMode loop_mode = LoopMode::DEFAULT;
  Trigger trigger = Trigger::ATTACK;
  int seq_length = 1;
  int seq_position = 1;
  std::vector<int> locc = std::vector<int> (128, 0);
  std::vector<int> hicc = std::vector<int> (128, 127);
  float ampeg_delay = 0;
  float ampeg_attack = 0;
  float ampeg_decay = 0;
  float ampeg_sustain = 100;
  float ampeg_release = 0;
  float volume = 0;
  float amp_veltrack = 100;
  float pan = 0;

  bool empty()
  {
    return sample == "";
  }

  /* playback state */
  int play_seq = 1;
};

namespace fs = std::filesystem; // FIXME

class Loader
{
  struct LineInfo
  {
    std::string  filename;

    int          number = 0;
    std::string  line;

    std::string
    location() const
    {
      return string_printf ("%s: line %d:",filename.c_str(), number);
    }
  };
  LineInfo current_line_info;
  std::set<std::string> preprocess_done;

public:
  enum class RegionType { NONE, GROUP, REGION };
  RegionType region_type = RegionType::NONE;
  Region active_group;
  Region active_region;
  std::vector<Region> regions;
  fs::path sample_path;
  SampleCache sample_cache; // FIXME: should share sample cache between different instances

  int
  convert_key (const std::string& k)
  {
    if (k.size() >= 2)
      {
        int offset = -1;

        std::string::const_iterator ki = k.begin();
        switch (tolower (*ki++))
          {
            case 'c': offset = 0; break;
            case 'd': offset = 2; break;
            case 'e': offset = 4; break;
            case 'f': offset = 5; break;
            case 'g': offset = 7; break;
            case 'a': offset = 9; break;
            case 'b': offset = 11; break;
          }
        if (offset >= 0)
          {
            if (*ki == '#')
              {
                offset++;
                ki++;
              }
            else if (*ki == 'b')
              {
                offset--;
                ki++;
              }
            // c4 should be 60
            return atoi (std::string (ki, k.end()).c_str()) * 12 + offset + 12;
          }
      }
    return atoi (k.c_str());
  }
  int
  convert_int (const std::string& s)
  {
    return atoi (s.c_str());
  }
  float
  convert_float (const std::string& s)
  {
    return atof (s.c_str());
  }
  Trigger
  convert_trigger (const std::string& t)
  {
    if (t == "release")
      return Trigger::RELEASE;
    return Trigger::ATTACK;
  }
  LoopMode
  convert_loop_mode (const std::string& l)
  {
    if (l == "no_loop")
      return LoopMode::NONE;
    else if (l == "one_shot")
      return LoopMode::ONE_SHOT;
    else if (l == "loop_continuous")
      return LoopMode::CONTINUOUS;
    else if (l == "loop_sustain")
      return LoopMode::SUSTAIN;
    log_warning ("%s unknown loop mode: %s\n", location().c_str(), l.c_str());
    return LoopMode::DEFAULT;
  }
  bool
  starts_with (const std::string& key, const std::string& start)
  {
    return key.substr (0, start.size()) == start;
  }
  void set_key_value (const std::string& key, const std::string& value);
  void
  handle_tag (const std::string& tag)
  {
    log_debug ("+++ TAG %s\n", tag.c_str());

    /* if we are done building a region, store it */
    if (tag == "region" || tag == "group")
      if (!active_region.empty())
        {
          regions.push_back (active_region);
          active_region = Region();
        }

    if (tag == "region")
      {
        region_type   = RegionType::REGION;
        active_region = active_group; /* inherit parameters from group */
      }
    else if (tag == "group")
      {
        region_type  = RegionType::GROUP;
        active_group = Region();
      }
    else
      log_warning ("%s unsupported tag '<%s>'\n", location().c_str(), tag.c_str());
  }
  std::string
  location()
  {
    return string_printf ("%s: line %d:", current_line_info.filename.c_str(), current_line_info.number);
  }
  bool preprocess_line (const LineInfo& input_line_info, std::vector<LineInfo>& lines);
  bool preprocess_file (const std::string& filename, std::vector<LineInfo>& lines);
  bool parse (const std::string& filename);
};

}

#endif /* LIQUIDSFZ_LOADER_HH */
