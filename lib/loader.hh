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
#include <set>

#include "log.hh"
#include "samplecache.hh"
#include "curve.hh"

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
};

enum class OffMode {
  FAST,
  NORMAL,
  TIME
};

enum class XFCurve {
  POWER,
  GAIN
};

class CCParamVec
{
public:
  struct Entry
  {
    int cc = -1;
    float value = 0;
  };
  const std::vector<Entry>::const_iterator
  begin() const
  {
    return entries_.begin();
  }
  const std::vector<Entry>::const_iterator
  end() const
  {
    return entries_.end();
  }
  void
  set (int cc, float value)
  {
    for (auto& entry : entries_)
      {
        if (entry.cc == cc)
          {
            /* overwrite old value */
            entry.value = value;
            return;
          }
      }
    entries_.push_back ({cc, value});
  }
  bool
  contains (int cc) const
  {
    for (const auto& entry : entries_)
      if (entry.cc == cc)
        return true;
    return false;
  }
private:
  std::vector<Entry> entries_;
};

struct AmpParam
{
  explicit AmpParam (float b)
    : base (b)
  {
  }
  float   base = 0;
  float   vel2 = 0;
  CCParamVec cc_vec;
};

struct XFCC
{
  int cc = -1;
  int lo = -1;
  int hi = -1;
};

struct CCInfo
{
  int cc = -1;
  bool has_label = false;
  std::string label;
  int default_value = 0;
};

struct KeyInfo
{
  int key = -1;
  std::string label;
  bool is_switch = false;
};

struct Region
{
  std::string sample;
  std::shared_ptr<SampleCache::Entry> cached_sample;
  std::string location;

  bool switch_match = true;

  int lokey = 0;
  int hikey = 127;
  int lovel = 0;
  int hivel = 127;
  double lorand = 0;
  double hirand = 1;
  int pitch_keycenter = 60;
  int pitch_keytrack = 100;
  int loop_start = 0;
  int loop_end = 0;
  LoopMode loop_mode = LoopMode::NONE;
  bool have_loop_mode = false;
  bool have_loop_start = false;
  bool have_loop_end = false;
  Trigger trigger = Trigger::ATTACK;
  int seq_length = 1;
  int seq_position = 1;
  std::vector<int> locc = std::vector<int> (128, 0);
  std::vector<int> hicc = std::vector<int> (128, 127);

  /* amp envelope generator */
  AmpParam ampeg_delay    { 0 };
  AmpParam ampeg_attack   { 0 };
  AmpParam ampeg_hold     { 0 };
  AmpParam ampeg_decay    { 0 };
  AmpParam ampeg_sustain  { 100 };
  AmpParam ampeg_release  { 0 };
  Curve    amp_velcurve;

  float volume = 0;
  float amplitude = 100;
  float amp_veltrack = 100;
  float amp_random = 0;
  float pan = 0;
  float rt_decay = 0;
  uint group = 0;
  uint off_by = 0;
  OffMode off_mode = OffMode::FAST;
  float off_time = 0;
  float delay = 0;
  uint  offset = 0;
  uint  offset_random = 0;

  int sw_lokey = -1;
  int sw_hikey = -1;
  int sw_lolast = -1;
  int sw_hilast = -1;
  int sw_default = -1;
  std::string sw_label;

  int tune = 0;
  int transpose = 0;
  int pitch_random = 0;

  int bend_up = 200;
  int bend_down = -200;

  int xfin_lovel = 0;
  int xfin_hivel = 0;
  int xfout_lovel = 127;
  int xfout_hivel = 127;
  int xfin_lokey = 0;
  int xfin_hikey = 0;
  int xfout_lokey = 127;
  int xfout_hikey = 127;
  std::vector<XFCC> xfin_ccs;
  std::vector<XFCC> xfout_ccs;

  XFCurve xf_velcurve = XFCurve::POWER;
  XFCurve xf_keycurve = XFCurve::POWER;
  XFCurve xf_cccurve = XFCurve::POWER;

  CCParamVec pan_cc;
  CCParamVec gain_cc;
  CCParamVec amplitude_cc;
  CCParamVec tune_cc;
  CCParamVec delay_cc;
  CCParamVec offset_cc;

  bool empty()
  {
    return sample == "";
  }

  /* playback state */
  int play_seq = 1;
};

struct SetCC
{
  int cc;
  int value;
};

struct Control
{
  std::string default_path;
  struct Define
  {
    std::string variable;
    std::string value;
  };
  std::vector<Define> defines;
  std::vector<SetCC> set_cc;
};

class Synth;

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
  Synth *synth_ = nullptr;

  bool parse_amp_param (AmpParam& amp_param, const std::string& key, const std::string& value, const std::string& param_str);
public:
  Loader (Synth *synth)
  {
    synth_ = synth;
  }
  bool in_control = false;
  enum class RegionType { NONE, GLOBAL, MASTER, GROUP, REGION };
  RegionType region_type = RegionType::NONE;
  Region active_global;
  Region active_master;
  Region active_group;
  Region active_region;
  bool   have_master = false;
  bool   have_group = false;
  std::vector<Region> regions;
  Control control;
  std::vector<CCInfo> cc_list;
  std::map<int, KeyInfo> key_map;
  std::vector<KeyInfo> key_list;
  CurveTable curve_table;
  std::string sample_path;

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
  unsigned int
  convert_uint (const std::string& s)
  {
    return strtoul (s.c_str(), nullptr, 10);
  }
  float
  convert_float (const std::string& s)
  {
    return string_to_double (s);
  }
  Trigger
  convert_trigger (const std::string& t)
  {
    if (t == "release")
      return Trigger::RELEASE;
    return Trigger::ATTACK;
  }
  LoopMode convert_loop_mode (const std::string& l);
  OffMode convert_off_mode (const std::string& s);
  XFCurve convert_xfcurve (const std::string& c);
  bool
  starts_with (const std::string& key, const std::string& start)
  {
    return key.substr (0, start.size()) == start;
  }
  bool split_sub_key (const std::string& key, const std::string& start, int& sub_key);
  XFCC& search_xfcc (std::vector<XFCC>& xfcc_vec, int cc, int def);
  CCInfo& update_cc_info (int cc);
  KeyInfo& update_key_info (int key);
  void set_key_value (const std::string& key, const std::string& value);
  void set_key_value_control (const std::string& key, const std::string& value);
  void handle_tag (const std::string& tag);
  std::string
  location()
  {
    return string_printf ("%s: line %d:", current_line_info.filename.c_str(), current_line_info.number);
  }
  void replace_defines (std::string& line);
  bool preprocess_line (const LineInfo& input_line_info, std::vector<LineInfo>& lines);
  bool preprocess_file (const std::string& filename, std::vector<LineInfo>& lines, const std::string& content_str = "");
  bool parse (const std::string& filename, SampleCache& sample_cache);
};

}

#endif /* LIQUIDSFZ_LOADER_HH */
