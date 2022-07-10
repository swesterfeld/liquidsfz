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
#include "filter.hh"

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
    int curvecc = 0;
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
  bool
  empty() const
  {
    return entries_.empty();
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
    Entry entry;
    entry.cc = cc;
    entry.value = value;
    entries_.push_back (entry);
  }
  void
  set_curvecc (int cc, int curvecc)
  {
    for (auto& entry : entries_)
      {
        if (entry.cc == cc)
          {
            /* overwrite old value */
            entry.curvecc = curvecc;
            return;
          }
      }
    Entry entry;
    entry.cc = cc;
    entry.curvecc = curvecc;
    entries_.push_back (entry);
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

struct EGParam
{
  explicit EGParam (float b)
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

struct FilterParams
{
  Filter::Type type = Filter::Type::LPF_2P;
  float        cutoff = -1;
  float        resonance = 0;
  CCParamVec   cutoff_cc;
  CCParamVec   resonance_cc;
  int          keytrack = 0;
  int          keycenter = 60;
  int          veltrack = 0;
};

struct LFOParams
{
  int   id = -1;
  float freq = 0;
  int   wave = 0; // default to triangle
  float delay = 0;
  float fade = 0;
  float phase = 0;

  float pitch = 0;
  float volume = 0;
  float cutoff = 0;

  CCParamVec freq_cc;
  CCParamVec delay_cc;
  CCParamVec fade_cc;
  CCParamVec phase_cc;

  CCParamVec pitch_cc;
  CCParamVec volume_cc;
  CCParamVec cutoff_cc;

  struct LFOMod {
    int        to_index = -1;
    float      lfo_freq = 0;
    CCParamVec lfo_freq_cc;
  };
  std::vector<LFOMod> lfo_mods; // LFO modulating LFO
};

struct SimpleLFO
{
  enum Type { PITCH, AMP, FIL };
  bool  used = false;
  float delay = 0;
  float fade = 0;
  float freq = 0;
  float depth = 0;

  CCParamVec freq_cc;
  CCParamVec depth_cc;
};

struct CurveSection
{
  int   curve_index = -1;
  Curve curve;

  bool
  empty()
  {
    return curve_index < 0;
  }
};

struct Limits
{
  size_t max_lfos = 0;
  size_t max_lfo_mods = 0;
};

struct Region
{
  std::string sample;
  std::string location;

  SampleP              cached_sample;
  Sample::PreloadInfoP preload_info;

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
  EGParam ampeg_delay    { 0 };
  EGParam ampeg_attack   { 0 };
  EGParam ampeg_hold     { 0 };
  EGParam ampeg_decay    { 0 };
  EGParam ampeg_sustain  { 100 };
  EGParam ampeg_release  { 0 };
  Curve   amp_velcurve;

  /* fil envelope generator */
  EGParam fileg_depth    { 0 };
  EGParam fileg_delay    { 0 };
  EGParam fileg_attack   { 0 };
  EGParam fileg_hold     { 0 };
  EGParam fileg_decay    { 0 };
  EGParam fileg_sustain  { 100 };
  EGParam fileg_release  { 0 };

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

  FilterParams fil, fil2;

  std::vector<LFOParams> lfos;

  SimpleLFO amplfo, pitchlfo, fillfo;

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
  Synth *synth_ = nullptr;

  bool find_variable (const std::string& line, Control::Define& out_define);
  bool parse_eg_param (const std::string& eg, EGParam& amp_param, const std::string& key, const std::string& value, const std::string& param_str);
  bool parse_ampeg_param (EGParam& amp_param, const std::string& key, const std::string& value, const std::string& param_str);
  bool parse_fileg_param (EGParam& amp_param, const std::string& key, const std::string& value, const std::string& param_str);
  bool parse_cc (const std::string& key, const std::string& value, CCParamVec& ccvec, const std::vector<std::string>& opcodes);
  template <class ... Args>
  bool parse_cc (const std::string& key, const std::string& value, CCParamVec& ccvec, Args ... args) /* make it possible to pass any number of strings here */
  {
    std::vector<std::string> opcodes;
    for (auto s : {args...})
      opcodes.push_back (s);
    return parse_cc (key, value, ccvec, opcodes);
  }
  int lfo_index_by_id (Region& region, int id);
  int lfo_mod_index_by_dest_id (Region& region, int lfo_index, int dest_id);
  int find_unused_lfo_id (Region& region);
  bool parse_freq_cc_lfo (Region& region, int lfo_index, const std::string& lfo_key, const std::string& value);
  bool parse_lfo_param (Region& region, const std::string& key, const std::string& value);
  bool parse_simple_lfo_param (Region& region, const std::string& type, SimpleLFO& lfo, const std::string& key, const std::string& value);
  void convert_lfo (Region& region, SimpleLFO& simple_lfo, SimpleLFO::Type type);
  float get_cc_vec_max (const CCParamVec& cc_param_vec);
  float get_cc_curve_max (const CCParamVec::Entry& entry);

  static constexpr int MAX_INCLUDE_DEPTH = 25;
public:
  Loader (Synth *synth)
  {
    synth_ = synth;
  }
  bool in_control = false;
  bool in_curve = false;
  CurveSection active_curve_section;
  enum class RegionType { NONE, GLOBAL, MASTER, GROUP, REGION };
  RegionType region_type = RegionType::NONE;
  Region active_global;
  Region active_master;
  Region active_group;
  Region active_region;
  bool   have_master = false;
  bool   have_group = false;
  std::vector<Region> regions;
  std::vector<Curve>  curves;
  Control control;
  std::vector<CCInfo> cc_list;
  std::map<int, KeyInfo> key_map;
  std::vector<KeyInfo> key_list;
  CurveTable curve_table;
  Limits limits;
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
  Filter::Type convert_filter_type (const std::string& f);
  int convert_wave (const std::string& w);
  bool
  starts_with (const std::string& key, const std::string& start)
  {
    return key.substr (0, start.size()) == start;
  }
  bool
  ends_with (const std::string& key, const std::string& end)
  {
    return (key.length() >= end.length()) && key.compare (key.length() - end.length(), end.length(), end) == 0;
  }
  bool split_sub_key (const std::string& key, const std::string& start, int& sub_key);
  XFCC& search_xfcc (std::vector<XFCC>& xfcc_vec, int cc, int def);
  CCInfo& update_cc_info (int cc);
  KeyInfo& update_key_info (int key);
  void set_key_value (const std::string& key, const std::string& value);
  void set_key_value_control (const std::string& key, const std::string& value);
  void set_key_value_curve (const std::string& key, const std::string& value);
  void handle_tag (const std::string& tag);
  void add_curve (const CurveSection& curve);
  void init_default_curves();
  std::string
  location()
  {
    return string_printf ("%s: line %d:", current_line_info.filename.c_str(), current_line_info.number);
  }
  bool preprocess_file (const std::string& filename, std::vector<LineInfo>& lines, int level, const std::string& content_str = "");
  bool parse (const std::string& filename, SampleCache& sample_cache);
};

}

#endif /* LIQUIDSFZ_LOADER_HH */
