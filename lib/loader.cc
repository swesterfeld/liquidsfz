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

#include "loader.hh"
#include "synth.hh"
#include "hydrogenimport.hh"

#include <algorithm>
#include <regex>

#include <assert.h>

using std::string;
using std::vector;
using std::regex;
using std::regex_replace;

using namespace LiquidSFZInternal;

static string
strip_spaces (const string& s)
{
  static const regex lws_re ("^\\s*");
  static const regex tws_re ("\\s*$");

  string r = regex_replace (s, tws_re, "");
  return regex_replace (r, lws_re, "");
}

LoopMode
Loader::convert_loop_mode (const string& l)
{
  if (l == "no_loop")
    return LoopMode::NONE;
  else if (l == "one_shot")
    return LoopMode::ONE_SHOT;
  else if (l == "loop_continuous")
    return LoopMode::CONTINUOUS;
  else if (l == "loop_sustain")
    return LoopMode::SUSTAIN;
  synth_->warning ("%s unknown loop mode: %s\n", location().c_str(), l.c_str());
  return LoopMode::NONE;
}

OffMode
Loader::convert_off_mode (const string& m)
{
  if (m == "fast")
    return OffMode::FAST;
  else if (m == "normal")
    return OffMode::NORMAL;
  else if (m == "time")
    return OffMode::TIME;
  synth_->warning ("%s unknown off mode: %s\n", location().c_str(), m.c_str());
  return OffMode::FAST;
}

XFCurve
Loader::convert_xfcurve (const string& c)
{
  if (c == "power")
    return XFCurve::POWER;
  else if (c == "gain")
    return XFCurve::GAIN;
  synth_->warning ("%s unknown crossfade curve: %s\n", location().c_str(), c.c_str());
  return XFCurve::POWER;
}

Filter::Type
Loader::convert_filter_type (const string& f)
{
  Filter::Type type = Filter::type_from_string (f);
  if (type != Filter::Type::NONE)
    return type;

  synth_->warning ("%s unsupported filter type: %s\n", location().c_str(), f.c_str());
  return Filter::Type::NONE;
}

bool
Loader::split_sub_key (const string& key, const string& start, int& sub_key)
{
  if (!starts_with (key, start))
    return false;
  if (key.length() <= start.length())
    return false;

  sub_key = convert_int (key.substr (start.length()));
  return true;
}

XFCC&
Loader::search_xfcc (vector<XFCC>& xfcc_vec, int cc, int def)
{
  for (auto& xfcc : xfcc_vec)
    {
      if (xfcc.cc == cc)
        return xfcc;
    }
  update_cc_info (cc);
  XFCC xfcc;
  xfcc.cc = cc;
  xfcc.lo = def;
  xfcc.hi = def;
  return xfcc_vec.emplace_back (xfcc);
}

CCInfo&
Loader::update_cc_info (int cc)
{
  for (auto& cc_info : cc_list)
    {
      if (cc_info.cc == cc)
        return cc_info;
    }
  CCInfo cc_info;
  cc_info.cc = cc;
  return cc_list.emplace_back (cc_info);
}

KeyInfo&
Loader::update_key_info (int key)
{
  KeyInfo& key_info = key_map[key];
  key_info.key = key;
  return key_info;
}

bool
Loader::parse_eg_param (const string& eg, EGParam& amp_param, const std::string& key, const std::string& value, const std::string& param_str)
{
  string prefix = eg + "_";

  if (key == prefix + param_str) // *eg_attack,...
    {
      amp_param.base = convert_float (value);
      return true;
    }
  if (key == prefix + "vel2" + param_str) // *eg_vel2attack,...
    {
      amp_param.vel2 = convert_float (value);
      return true;
    }
  int sub_key;
  if (split_sub_key (key, prefix + param_str + "cc", sub_key)    // *eg_attackccN,...
  ||  split_sub_key (key, prefix + param_str + "_oncc", sub_key)) // *eg_attack_onccN,...
    {
      amp_param.cc_vec.set (sub_key, convert_float (value));
      update_cc_info (sub_key);
      return true;
    }
  if (split_sub_key (key, prefix + param_str + "_curvecc", sub_key)) // *eg_attack_curveccN,...
    {
      amp_param.cc_vec.set_curvecc (sub_key, convert_int (value));
      update_cc_info (sub_key);
      return true;
    }
  return false;
}

bool
Loader::parse_ampeg_param (EGParam& amp_param, const string& key, const string& value, const string& param_str)
{
  return parse_eg_param ("ampeg", amp_param, key, value, param_str);
}

bool
Loader::parse_fileg_param (EGParam& amp_param, const string& key, const string& value, const string& param_str)
{
  return parse_eg_param ("fileg", amp_param, key, value, param_str);
}

bool
Loader::parse_cc (const std::string& key, const std::string& value, CCParamVec& ccvec, const std::vector<std::string>& opcodes)
{
  for (auto opcode : opcodes)
    {
      int sub_key;

      if (split_sub_key (key, opcode, sub_key))
        {
          if (ends_with (opcode, "_cc") || ends_with (opcode, "_oncc"))
            {
              ccvec.set (sub_key, convert_float (value));
              update_cc_info (sub_key);
              return true;
            }
          else if (ends_with (opcode, "_curvecc"))
            {
              ccvec.set_curvecc (sub_key, convert_int (value));
              update_cc_info (sub_key);
              return true;
            }
        }
    }
  return false;
}

void
Loader::set_key_value (const string& key, const string& value)
{
  if (in_control)
    {
      set_key_value_control (key, value);
      return;
    }
  else if (in_curve)
    {
      set_key_value_curve (key, value);
      return;
    }
  /* handle global, group and region levels */
  Region *region_ptr = nullptr;
  switch (region_type)
    {
      case RegionType::GLOBAL:  region_ptr = &active_global;
                                break;
      case RegionType::MASTER:  region_ptr = &active_master;
                                break;
      case RegionType::GROUP:   region_ptr = &active_group;
                                break;
      case RegionType::REGION:  region_ptr = &active_region;
                                break;
      default:                  return;
    }

  Region& region = *region_ptr;
  int sub_key;
  synth_->debug ("+++ '%s' = '%s'\n", key.c_str(), value.c_str());
  if (key == "sample")
    {
      // on unix, convert \-seperated filename to /-separated filename
      string native_filename = value;
      std::replace (native_filename.begin(), native_filename.end(), '\\', PATH_SEPARATOR);

      string path = sample_path;
      if (control.default_path != "")
        path = path_join (path, control.default_path);

      path = path_join (path, native_filename);
      region.sample = path_absolute (path);
      region.location = location();
    }
  else if (key == "lokey")
    region.lokey = convert_key (value);
  else if (key == "hikey")
    region.hikey = convert_key (value);
  else if (key == "key")
    {
      const int k = convert_key (value);
      region.lokey = k;
      region.hikey = k;
      region.pitch_keycenter = k;
    }
  else if (key == "lovel")
    region.lovel = convert_key (value);
  else if (key == "hivel")
    region.hivel = convert_key (value);
  else if (key == "pitch_keycenter")
    region.pitch_keycenter = convert_key (value);
  else if (key == "pitch_keytrack")
    region.pitch_keytrack = convert_int (value);
  else if (key == "lorand")
    region.lorand = convert_float (value);
  else if (key == "hirand")
    region.hirand = convert_float (value);
  else if (key == "loop_mode")
    {
      region.loop_mode = convert_loop_mode (value);
      region.have_loop_mode = true;
    }
  else if (key == "loop_start")
    {
      region.loop_start = convert_int (value);
      region.have_loop_start = true;
    }
  else if (key == "loop_end")
    {
      region.loop_end = convert_int (value);
      region.have_loop_end = true;
    }
  else if (split_sub_key (key, "locc", sub_key))
    {
      int cc = sub_key;
      if (cc >= 0 && cc <= 127)
        {
          region.locc[cc] = convert_int (value);
          update_cc_info (cc);
        }
    }
  else if (split_sub_key (key, "hicc", sub_key))
    {
      int cc = sub_key;
      if (cc >= 0 && cc <= 127)
        {
          region.hicc[cc] = convert_int (value);
          update_cc_info (cc);
        }
    }
  else if (starts_with (key, "on_locc") || starts_with (key, "on_hicc"))
    region.trigger = Trigger::CC;
  else if (key == "trigger")
    region.trigger = convert_trigger (value);
  else if (key == "seq_length")
    region.seq_length = convert_int (value);
  else if (key == "seq_position")
    region.seq_position = convert_int (value);
  else if (parse_ampeg_param (region.ampeg_delay, key, value, "delay") ||
           parse_ampeg_param (region.ampeg_attack, key, value, "attack") ||
           parse_ampeg_param (region.ampeg_hold, key, value, "hold") ||
           parse_ampeg_param (region.ampeg_decay, key, value, "decay") ||
           parse_ampeg_param (region.ampeg_sustain, key, value, "sustain") ||
           parse_ampeg_param (region.ampeg_release, key, value, "release"))
    {
      // actual value conversion is performed by parse_ampeg_param
    }
  else if (parse_fileg_param (region.fileg_depth, key, value, "depth") ||
           parse_fileg_param (region.fileg_delay, key, value, "delay") ||
           parse_fileg_param (region.fileg_attack, key, value, "attack") ||
           parse_fileg_param (region.fileg_hold, key, value, "hold") ||
           parse_fileg_param (region.fileg_decay, key, value, "decay") ||
           parse_fileg_param (region.fileg_sustain, key, value, "sustain") ||
           parse_fileg_param (region.fileg_release, key, value, "release"))
    {
      // actual value conversion is performed by parse_fileg_param
    }
  else if (split_sub_key (key, "amp_velcurve_", sub_key))
    region.amp_velcurve.set (sub_key, convert_float (value));
  else if (key == "volume")
    region.volume = convert_float (value);
  else if (key == "amplitude")
    region.amplitude = convert_float (value);
  else if (key == "amp_veltrack")
    region.amp_veltrack = convert_float (value);
  else if (key == "amp_random")
    region.amp_random = convert_float (value);
  else if (key == "pan")
    region.pan = convert_float (value);
  else if (key == "rt_decay")
    region.rt_decay = convert_float (value);
  else if (key == "group")
    region.group = convert_uint (value);
  else if (key == "off_by")
    region.off_by = convert_uint (value);
  else if (key == "off_mode")
    region.off_mode = convert_off_mode (value);
  else if (key == "off_time")
    region.off_time = convert_float (value);
  else if (key == "delay")
    region.delay = convert_float (value);
  else if (key == "offset")
    region.offset = convert_uint (value);
  else if (key == "offset_random")
    region.offset_random = convert_uint (value);
  else if (key == "sw_lokey")
    region.sw_lokey = convert_key (value);
  else if (key == "sw_hikey")
    region.sw_hikey = convert_key (value);
  else if (key == "sw_last")
    region.sw_lolast = region.sw_hilast = convert_key (value);
  else if (key == "sw_lolast")
    region.sw_lolast = convert_key (value);
  else if (key == "sw_hilast")
    region.sw_hilast = convert_key (value);
  else if (key == "sw_default")
    region.sw_default = convert_key (value);
  else if (key == "sw_label")
    region.sw_label = value;
  else if (key == "tune")
    region.tune = convert_int (value);
  else if (key == "transpose")
    region.transpose = convert_int (value);
  else if (key == "pitch_random")
    region.pitch_random = convert_int (value);
  else if (key == "bend_up")
    region.bend_up = convert_int (value);
  else if (key == "bend_down")
    region.bend_down = convert_int (value);
  else if (key == "cutoff")
    region.fil.cutoff = convert_float (value);
  else if (key == "cutoff2")
    region.fil2.cutoff = convert_float (value);
  else if (key == "resonance")
    region.fil.resonance = convert_float (value);
  else if (key == "resonance2")
    region.fil2.resonance = convert_float (value);
  else if (key == "fil_type")
    region.fil.type = convert_filter_type (value);
  else if (key == "fil2_type")
    region.fil2.type = convert_filter_type (value);
  else if (key == "fil_keytrack")
    region.fil.keytrack = convert_int (value);
  else if (key == "fil2_keytrack")
    region.fil2.keytrack = convert_int (value);
  else if (key == "fil_keycenter")
    region.fil.keycenter = convert_key (value);
  else if (key == "fil2_keycenter")
    region.fil2.keycenter = convert_key (value);
  else if (key == "fil_veltrack")
    region.fil.veltrack = convert_int (value);
  else if (key == "fil2_veltrack")
    region.fil2.veltrack = convert_int (value);
  else if (parse_cc (key, value, region.pan_cc,
                     "pan_cc",
                     "pan_oncc",
                     "pan_curvecc")

       ||  parse_cc (key, value, region.gain_cc,
                     "gain_cc",
                     "volume_cc",
                     "volume_oncc",
                     "volume_curvecc")

       ||  parse_cc (key, value, region.amplitude_cc,
                     "amplitude_cc",
                     "amplitude_oncc",
                     "amplitude_curvecc")

       ||  parse_cc (key, value, region.tune_cc,
                     "tune_cc",
                     "tune_oncc",
                     "pitch_oncc", /* sforzando supports 3 variants */
                     "pitch_curvecc")

       ||  parse_cc (key, value, region.delay_cc,
                     "delay_oncc",
                     "delay_curvecc")

       ||  parse_cc (key, value, region.fil.cutoff_cc,
                     "cutoff_cc",
                     "cutoff_oncc",
                     "cutoff_curvecc")

       ||  parse_cc (key, value, region.fil2.cutoff_cc,
                     "cutoff2_cc",
                     "cutoff2_oncc",
                     "cutoff2_curvecc")

       ||  parse_cc (key, value, region.fil.resonance_cc,
                     "resonance_cc",
                     "resonance_oncc",
                     "resonance_curvecc")

       ||  parse_cc (key, value, region.fil2.resonance_cc,
                     "resonance2_cc",
                     "resonance2_oncc",
                     "resonance2_curvecc")

       ||  parse_cc (key, value, region.offset_cc,
                     "offset_cc",
                     "offset_oncc"))
    {
      // actual value conversion is performed by parse_cc
    }
  else if (key == "xfin_lovel")
    region.xfin_lovel = convert_int (value);
  else if (key == "xfin_hivel")
    region.xfin_hivel = convert_int (value);
  else if (key == "xfout_lovel")
    region.xfout_lovel = convert_int (value);
  else if (key == "xfout_hivel")
    region.xfout_hivel = convert_int (value);
  else if (key == "xfin_lokey")
    region.xfin_lokey = convert_key (value);
  else if (key == "xfin_hikey")
    region.xfin_hikey = convert_key (value);
  else if (key == "xfout_lokey")
    region.xfout_lokey = convert_key (value);
  else if (key == "xfout_hikey")
    region.xfout_hikey = convert_key (value);
  else if (split_sub_key (key, "xfin_locc", sub_key))
    {
      XFCC& xfcc = search_xfcc (region.xfin_ccs, sub_key, 0);
      xfcc.lo = convert_int (value);
    }
  else if (split_sub_key (key, "xfin_hicc", sub_key))
    {
      XFCC& xfcc = search_xfcc (region.xfin_ccs, sub_key, 0);
      xfcc.hi = convert_int (value);
    }
  else if (split_sub_key (key, "xfout_locc", sub_key))
    {
      XFCC& xfcc = search_xfcc (region.xfout_ccs, sub_key, 127);
      xfcc.lo = convert_int (value);
    }
  else if (split_sub_key (key, "xfout_hicc", sub_key))
    {
      XFCC& xfcc = search_xfcc (region.xfout_ccs, sub_key, 127);
      xfcc.hi = convert_int (value);
    }
  else if (key == "xf_velcurve")
    region.xf_velcurve = convert_xfcurve (value);
  else if (key == "xf_keycurve")
    region.xf_keycurve = convert_xfcurve (value);
  else if (key == "xf_cccurve")
    region.xf_cccurve = convert_xfcurve (value);
  else
    synth_->warning ("%s unsupported opcode '%s'\n", location().c_str(), key.c_str());
}

void
Loader::set_key_value_control (const string& key, const string& value)
{
  int sub_key;

  if (key == "default_path")
    {
      string native_path = value;
      std::replace (native_path.begin(), native_path.end(), '\\', PATH_SEPARATOR);

      control.default_path = native_path;
    }
  else if (split_sub_key (key, "set_cc", sub_key))
    {
      SetCC set_cc;
      set_cc.cc = sub_key;
      set_cc.value = convert_int (value);
      control.set_cc.push_back (set_cc);

      CCInfo& cc_info = update_cc_info (set_cc.cc);
      cc_info.default_value = set_cc.value;
    }
  else if (split_sub_key (key, "label_cc", sub_key))
    {
      CCInfo& cc_info = update_cc_info (sub_key);
      cc_info.has_label = true;
      cc_info.label = value;
    }
  else if (split_sub_key (key, "label_key", sub_key))
    {
      update_key_info (sub_key).label = value;
    }
  else
    synth_->warning ("%s unsupported opcode '%s'\n", location().c_str(), key.c_str());
}

void
Loader::set_key_value_curve (const string& key, const string& value)
{
  int sub_key;

  if (key == "curve_index")
    {
      int i = convert_int (value);

      if (i >= 0 && i < 256)
        active_curve_section.curve_index = convert_int (value);
      else
        synth_->warning ("%s bad curve_index '%d' (should be in range [0,255])\n", location().c_str(), i);
    }
  else if (split_sub_key (key, "v", sub_key))
    {
      active_curve_section.curve.set (sub_key, convert_float (value));
    }
  else
    synth_->warning ("%s unsupported opcode '%s'\n", location().c_str(), key.c_str());
}

void
Loader::handle_tag (const std::string& tag)
{
  synth_->debug ("+++ TAG %s\n", tag.c_str());

  /* if we are done building a region, store it */
  if (tag == "region" || tag == "group" || tag == "master" || tag == "global")
    if (!active_region.empty())
      {
        regions.push_back (active_region);
        active_region = Region();
      }

  if (!active_curve_section.empty())
    {
      add_curve (active_curve_section);
      active_curve_section = CurveSection();
    }

  in_control = false;
  in_curve = false;
  if (tag == "control")
    {
      in_control = true;
      control = Control();
    }
  else if (tag == "curve")
    {
      in_curve = true;
      active_curve_section = CurveSection();
    }
  else if (tag == "region")
    {
      /* inherit region parameters from parent region */
      if (have_group)
        active_region = active_group;
      else if (have_master)
        active_region = active_master;
      else
        active_region = active_global;

      region_type = RegionType::REGION;
    }
  else if (tag == "group")
    {
      /* inherit region parameters from parent region */
      if (have_master)
        active_group = active_master;
      else
        active_group = active_global;

      region_type = RegionType::GROUP;
      have_group  = true;
    }
  else if (tag == "master")
    {
      /* inherit region parameters from parent region */
      active_master = active_global;

      region_type = RegionType::MASTER;
      have_group  = false;
      have_master = true;
    }
  else if (tag == "global")
    {
      active_global = Region();

      region_type = RegionType::GLOBAL;
      have_group  = false;
      have_master = false;
    }
  else
    synth_->warning ("%s unsupported tag '<%s>'\n", location().c_str(), tag.c_str());
}

void
Loader::add_curve (const CurveSection& c)
{
  if (c.curve_index < 0 || c.curve_index > 255)
    return;

  if (curves.size() <= uint (c.curve_index))
    curves.resize (c.curve_index + 1);

  curves[c.curve_index] = c.curve;
}

bool
Loader::preprocess_line (const LineInfo& input_line_info, vector<LineInfo>& lines)
{
  // strip comments
  static const regex comment_re ("//.*$");
  string line = regex_replace (input_line_info.line, comment_re, "");

  std::smatch sm;

  // handle #define
  static const regex define_re ("\\s*#\\s*define\\s+(\\$\\S+)\\s([^$]*)");
  if (regex_match (line, sm, define_re))
    {
      // by our regex, values cannot contain $, to prevent problems in replace_defines() later on

      Control::Define define;
      define.variable = sm[1];
      define.value    = strip_spaces (sm[2]);
      control.defines.push_back (define);

      line = "";
    }

  // perform variable substitution as required by #define
  replace_defines (line);

  // detect #include
  static const regex include_re ("\\s*#\\s*include\\s+[\"<](.*)[\">]\\s*");
  if (regex_match (line, sm, include_re))
    {
      string include_filename = path_absolute (path_join (sample_path, sm[1].str()));

      if (!preprocess_done.count (include_filename)) // prevent infinite recursion for buggy .sfz
        {
          bool inc_ok = preprocess_file (path_absolute (path_join (sample_path, sm[1].str())), lines);
          if (!inc_ok)
            synth_->error ("%s unable to read #include '%s'\n", input_line_info.location().c_str(), include_filename.c_str());

          return inc_ok;
        }
      else
        synth_->warning ("%s skipping duplicate #include '%s'\n", input_line_info.location().c_str(), include_filename.c_str());
    }
  else
    {
      auto linfo = input_line_info;
      linfo.line = line;
      lines.push_back (linfo);
    }
  return true;
}

void
Loader::replace_defines (string& line)
{
  size_t start_pos = 0;

  while ((start_pos = line.find ('$', start_pos)) != string::npos)
    {
      const string test_var = line.substr (start_pos);

      for (const auto& define : control.defines)
        {
          if (starts_with (test_var, define.variable))
            {
              line.replace (start_pos, define.variable.length(), define.value);
              break;
            }
        }
      start_pos++;
    }
}

static bool
load_file (FILE *file, vector<char>& contents)
{
  contents.clear();

  /* read file into memory */
  char buffer[1024];

  size_t l;
  while (!feof (file) && (l = fread (buffer, 1, sizeof (buffer), file)) > 0)
    contents.insert (contents.end(), buffer, buffer + l);

  return !ferror (file);
}

bool
Loader::preprocess_file (const std::string& filename, vector<LineInfo>& lines, const std::string& content_str)
{
  vector<char> contents;

  if (content_str != "")
    {
      contents.insert (contents.begin(), content_str.begin(), content_str.end());
    }
  else
    {
      FILE *file = fopen (filename.c_str(), "r");
      if (!file)
        return false;

      bool read_ok = load_file (file, contents);
      fclose (file);

      if (!read_ok)
        return false;
    }
  preprocess_done.insert (filename);

  LineInfo line_info;
  line_info.filename = filename;
  line_info.number = 1;

  for (char ch : contents)
    {
      if (ch != '\r' && ch != '\n')
        {
          line_info.line += ch;
        }
      else
        {
          if (ch == '\n')
            {
              if (!preprocess_line (line_info, lines))
                return false;

              line_info.number++;
              line_info.line = "";
            }
        }
    }
  if (!line_info.line.empty())
    {
      if (!preprocess_line (line_info, lines))
        return false;
    }
  return true;
}

bool
Loader::parse (const string& filename, SampleCache& sample_cache)
{
  sample_path = path_dirname (filename);

  // read file
  vector<LineInfo> lines;

  HydrogenImport himport (synth_);
  if (himport.detect (filename))
    {
      string out;
      if (himport.parse (filename, out))
        {
          if (!preprocess_file (filename, lines, out))
            {
              synth_->error ("error reading converted hydrogen drumkit '%s'\n", filename.c_str());
              return false;
            }
        }
      else
        {
          synth_->error ("error reading hydrogen drumkit '%s'\n", filename.c_str());
          return false;
        }
    }
  else
    {
      if (!preprocess_file (filename, lines))
        {
          synth_->error ("error reading file '%s'\n", filename.c_str());
          return false;
        }
    }

  static const regex space_re ("\\s+(.*)");
  static const regex tag_re ("<([^>]*)>(.*)");
  static const regex key_val_re ("([a-z0-9_]+)=(\\S+)(.*)");
  for (auto line_info : lines)
    {
      current_line_info = line_info; // for error location reporting

      auto l = line_info.line;
      while (l.size())
        {
          std::smatch sm;
          //printf ("@%s@\n", l.c_str());
          if (regex_match (l, sm, space_re))
            {
              l = sm[1];
            }
          else if (regex_match (l, sm, tag_re))
            {
              handle_tag (sm[1].str());
              l = sm[2];
            }
          else if (regex_match (l, key_val_re))
            {
              /* we need to handle three cases to deal with values that may contain spaces:
               *
               * - value extends until end of line
               * - value is followed by <tag>
               * - value is followed by another opcode (foo=bar)
               */
              static const regex key_val_space_re_eol ("([a-z0-9_]+)=([^=<]+)");
              static const regex key_val_space_re_tag ("([a-z0-9_]+)=([^=<]+)(<.*)");
              static const regex key_val_space_re_eq ("([a-z0-9_]+)=([^=<]+)(\\s[a-z0-9_]+=.*)");

              if (regex_match (l, sm, key_val_space_re_eol))
                {
                  set_key_value (sm[1].str(), strip_spaces (sm[2].str()));
                  l = "";
                }
              else if (regex_match (l, sm, key_val_space_re_tag))
                {
                  set_key_value (sm[1].str(), strip_spaces (sm[2].str()));
                  l = sm[3]; // parse rest
                }
              else if (regex_match (l, sm, key_val_space_re_eq))
                {
                  set_key_value (sm[1].str(), strip_spaces (sm[2].str()));
                  l = sm[3]; // parse rest
                }
              else
                {
                  synth_->error ("%s parse error in opcode parsing\n", location().c_str());
                  return false;
                }
            }
          else
            {
              synth_->error ("%s toplevel parsing failed\n", location().c_str());
              return false;
            }
        }
    }
  if (!active_region.empty())
    regions.push_back (active_region);

  if (!active_curve_section.empty())
    add_curve (active_curve_section);

  synth_->progress (0);
  for (size_t i = 0; i < regions.size(); i++)
    {
      Region& region = regions[i];

      const auto cached_sample = sample_cache.load (region.sample);
      region.cached_sample = cached_sample;

      if (!cached_sample)
        synth_->warning ("%s: missing sample: '%s'\n", filename.c_str(), region.sample.c_str());

      if (cached_sample && cached_sample->loop)
        {
          /* if values have been given explicitely, keep them
           *   -> user can override loop settings from wav file (cached sample)
           */
          if (!region.have_loop_mode)
            region.loop_mode = LoopMode::CONTINUOUS;

          if (!region.have_loop_start)
            region.loop_start = cached_sample->loop_start;

          if (!region.have_loop_end)
            region.loop_end = cached_sample->loop_end;
        }
      if (region.fil.cutoff < 0) /* filter defaults to lpf_2p, but only if cutoff was found */
        region.fil.type = Filter::Type::NONE;

      if (region.fil2.cutoff < 0) /* filter 2 defaults to lpf_2p, but only if cutoff2 was found */
        region.fil2.type = Filter::Type::NONE;

      if (region.sw_lolast >= 0)
        region.switch_match = (region.sw_lolast <= region.sw_default && region.sw_hilast >= region.sw_default);

      curve_table.expand_curve (region.amp_velcurve);

      /* generate entries for regular notes */
      if (region.lokey > 0)
        for (int key = region.lokey; key <= region.hikey; key++)
          update_key_info (key).is_switch = false;

      /* generate entries for key switches */
      if (region.sw_lolast > 0)
        {
          for (int key = region.sw_lolast; key <= region.sw_hilast; key++)
            {
              KeyInfo& ki = update_key_info (key);
              ki.is_switch = true;
              if (region.sw_label != "")
                ki.label = region.sw_label;
            }
        }

      /* update progress info */
      synth_->progress ((i + 1) * 100.0 / regions.size());
    }
  // generate final cc_list
  for (const auto& [key, key_info] : key_map)
    key_list.push_back (key_info);

  // generate final key_list
  std::sort (cc_list.begin(), cc_list.end(),
    [] (const CCInfo& a, const CCInfo& b) {
      return a.cc < b.cc;
    });

  // finalize curves
  for (auto& c : curves)
    curve_table.expand_curve (c);

  synth_->debug ("*** regions: %zd\n", regions.size());
  return true;
}
