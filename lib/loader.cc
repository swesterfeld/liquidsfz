// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

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

int
Loader::convert_wave (const string& w)
{
  int wave = convert_int (w);
  if (LFOGen::supports_wave (wave))
    return wave;

  synth_->warning ("%s unsupported lfo wave type: %s\n", location().c_str(), w.c_str());
  return 0;
}

bool
Loader::split_sub_key (const string& key, const string& start, int& sub_key)
{
  if (!starts_with (key, start))
    return false;
  if (key.length() <= start.length())
    return false;

  string subkey_str = key.substr (start.length());
  for (auto c : subkey_str)
    if (!isdigit (c))
      return false;

  sub_key = convert_int (subkey_str);
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

SetCC&
Loader::update_set_cc (int cc, int value)
{
  for (auto& set_cc : control.set_cc)
    {
      if (set_cc.cc == cc)
        {
          set_cc.value = value;
          return set_cc;
        }
    }
  SetCC set_cc;
  set_cc.cc = cc;
  set_cc.value = value;
  return control.set_cc.emplace_back (set_cc);
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

int
Loader::find_unused_lfo_id (Region& region)
{
  for (int id = 1 ;; id++)
    {
      bool used = false;

      for (size_t i = 0; i < region.lfos.size(); i++)
        if (region.lfos[i].id == id)
          used = true;

      if (!used)
        return id;
    }
}

int
Loader::lfo_index_by_id (Region& region, int id)
{
  /* find existing LFO parameters */
  for (size_t i = 0; i < region.lfos.size(); i++)
    if (region.lfos[i].id == id)
      return i;

  /* create new LFO with this id */
  LFOParams lfo_params;
  lfo_params.id = id;
  region.lfos.push_back (lfo_params);

  return region.lfos.size() - 1;
}

int
Loader::lfo_mod_index_by_dest_id (Region& region, int l, int dest_id)
{
  /* search existing mod by id */
  int to_index = lfo_index_by_id (region, dest_id);
  for (size_t i = 0; i < region.lfos[l].lfo_mods.size(); i++)
    if (region.lfos[l].lfo_mods[i].to_index == to_index)
      return i;

  /* create new lfo_mod if necessary */
  LFOParams::LFOMod lfo_mod;
  lfo_mod.to_index = to_index;
  region.lfos[l].lfo_mods.push_back (lfo_mod);

  return region.lfos[l].lfo_mods.size() - 1;
}

bool
Loader::parse_freq_cc_lfo (Region& region, int l, const string& lfo_key, const string& value)
{
  /* parse opcodes like "lfo1_freq_lfo6_oncc1=3" */
  std::smatch sm;

  static const regex lfo_mod_re ("freq_lfo([0-9]+)(\\S+)");
  if (!regex_match (lfo_key, sm, lfo_mod_re))
    return false;

  int dest_lfo_id = convert_int (sm[1].str());
  int lfo_mod_index = lfo_mod_index_by_dest_id (region, l, dest_lfo_id);

  return parse_cc (sm[2].str(), value, region.lfos[l].lfo_mods[lfo_mod_index].lfo_freq_cc, "_*");
}

bool
Loader::parse_lfo_param (Region& region, const string& key, const string& value)
{
  if (!starts_with (key, "lfo"))
    return false;

  static const regex lfo_re ("lfo([0-9]+)_(\\S+)");
  std::smatch sm;
  if (!regex_match (key, sm, lfo_re))
    return false;

  int    l = lfo_index_by_id (region, convert_int (sm[1].str()));
  string lfo_key = sm[2].str();

  /* process opcodes */
  int sub_key;

  if (lfo_key == "freq")
    region.lfos[l].freq = convert_float (value);
  else if (lfo_key == "wave")
    region.lfos[l].wave = convert_wave (value);
  else if (lfo_key == "phase")
    region.lfos[l].phase = convert_float (value);
  else if (lfo_key == "delay")
    region.lfos[l].delay = convert_float (value);
  else if (lfo_key == "fade")
    region.lfos[l].fade = convert_float (value);
  else if (lfo_key == "pitch")
    region.lfos[l].pitch = convert_float (value);
  else if (lfo_key == "volume")
    region.lfos[l].volume = convert_float (value);
  else if (lfo_key == "cutoff")
    region.lfos[l].cutoff = convert_float (value);
  else if (split_sub_key (lfo_key, "freq_lfo", sub_key))
    {
      int lfo_mod_index = lfo_mod_index_by_dest_id (region, l, sub_key);

      region.lfos[l].lfo_mods[lfo_mod_index].lfo_freq = convert_float (value);
    }
  else if (parse_cc (lfo_key, value, region.lfos[l].freq_cc,    "freq_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].phase_cc,   "phase_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].delay_cc,   "delay_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].fade_cc,    "fade_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].pitch_cc,   "pitch_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].volume_cc,  "volume_*")
       ||  parse_cc (lfo_key, value, region.lfos[l].cutoff_cc,  "cutoff_*"))
    {
      // actual value conversion is performed by parse_cc
    }
  else if (parse_freq_cc_lfo (region, l, lfo_key, value))
    {
      // actual value conversion is performed by parse_freq_cc_lfo
    }
  else
    return false;

  return true;
}

bool
Loader::parse_simple_lfo_param (Region& region, const string& type, SimpleLFO& lfo, const string& key, const string& value)
{
  int sub_key;

  if (key == type + "freq")
    lfo.freq = convert_float (value);
  else if (key == type + "depth")
    lfo.depth = convert_float (value);
  else if (key == type + "fade")
    lfo.fade = convert_float (value);
  else if (key == type + "delay")
    lfo.delay = convert_float (value);
  else if (split_sub_key (key, type + "freqcc", sub_key))
    {
      lfo.freq_cc.set (sub_key, convert_float (value));
      update_cc_info (sub_key);
    }
  else if (split_sub_key (key, type + "depthcc", sub_key))
    {
      lfo.depth_cc.set (sub_key, convert_float (value));
      update_cc_info (sub_key);
    }
  else
    return false;

  lfo.used = true;
  return true;
}

bool
Loader::parse_cc (const std::string& key, const std::string& value, CCParamVec& ccvec, const std::vector<std::string>& opcodes)
{
  for (auto opcode : opcodes)
    {
      int sub_key;

      if (ends_with (opcode, "_*"))
        {
          opcode.pop_back();
          if (parse_cc (key, value, ccvec, opcode + "cc", opcode + "oncc", opcode + "curvecc"))
            return true;
        }
      else if (split_sub_key (key, opcode, sub_key))
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

      if (path_is_absolute (native_filename))
        {
          region.sample = native_filename;
        }
      else
        {
          string path = sample_path;
          if (control.default_path != "")
            path = path_join (path, control.default_path);

          path = path_join (path, native_filename);
          region.sample = path_absolute (path);
        }
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
  else if (key == "loop_mode" || key == "loopmode")
    {
      region.loop_mode = convert_loop_mode (value);
      region.have_loop_mode = true;
    }
  else if (key == "loop_start" || key == "loopstart")
    {
      region.loop_start = convert_int (value);
      region.have_loop_start = true;
    }
  else if (key == "loop_end" || key == "loopend")
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
  else if (key == "off_by" || key == "offby")
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
  else if (key == "tune" || key == "pitch")
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
                     "pan_*")
       ||  parse_cc (key, value, region.gain_cc,
                     "gain_cc", // acts as alias
                     "volume_*")
       ||  parse_cc (key, value, region.amplitude_cc,
                     "amplitude_*")
       ||  parse_cc (key, value, region.tune_cc,
                     "pitch_*", // sforzando supports both
                     "tune_*")
       ||  parse_cc (key, value, region.delay_cc,
                     "delay_*")
       ||  parse_cc (key, value, region.fil.cutoff_cc,
                     "cutoff_*")
       ||  parse_cc (key, value, region.fil2.cutoff_cc,
                     "cutoff2_*")
       ||  parse_cc (key, value, region.fil.resonance_cc,
                     "resonance_*")
       ||  parse_cc (key, value, region.fil2.resonance_cc,
                     "resonance2_*")
       ||  parse_cc (key, value, region.offset_cc,
                     "offset_*"))
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
  else if (parse_lfo_param (region, key, value))
    {
      // actual value conversion is performed by parse_lfo_param
    }
  else if (parse_simple_lfo_param (region, "pitchlfo_", region.pitchlfo, key, value)
       ||  parse_simple_lfo_param (region, "amplfo_",   region.amplfo,   key, value)
       ||  parse_simple_lfo_param (region, "fillfo_",   region.fillfo,   key, value))
    {
      // actual value conversion is performed by parse_simple_lfo_param
    }
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
      SetCC& set_cc = update_set_cc (sub_key, convert_int (value));

      CCInfo& cc_info = update_cc_info (set_cc.cc);
      cc_info.default_value = set_cc.value;
    }
  else if (split_sub_key (key, "set_hdcc", sub_key)
       ||  split_sub_key (key, "set_realcc", sub_key))
    {
      /* we don't really support floating point CCs
       * however we convert to ensure sane defaults for .sfz files using this opcode
       */
      int ivalue = std::clamp<int> (lrint (convert_float (value) * 127), 0, 127);
      SetCC& set_cc = update_set_cc (sub_key, ivalue);

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
Loader::init_default_curves()
{
  curves.resize (7);

  curves[0].set (0,   0);
  curves[0].set (127, 1);

  curves[1].set (0,  -1);
  curves[1].set (127, 1);

  curves[2].set (0,   1);
  curves[2].set (127, 0);

  curves[3].set (0,    1);
  curves[3].set (127, -1);

  for (int v = 0; v < 128; v++)
    {
      curves[4].set (v, (v * v) / (127.0 * 127.0));   // volume curve
      curves[5].set (v, sqrt (v / 127.0));            // xfin power curve
      curves[6].set (v, sqrt ((127 - v) / 127.0));    // xfout power curve
    }
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

static string
line_lookahead (vector<char>& contents, size_t pos)
{
  string result;
  while (pos < contents.size() && (contents[pos] != '\n' && contents[pos] != '\r'))
    result += contents[pos++];
  return result;
}

bool
Loader::find_variable (const string& line, Control::Define& out_define)
{
  size_t best_len = string::npos;
  for (const auto& define : control.defines)
    {
      if (starts_with (line, define.variable) && (define.variable.length() < best_len))
        {
          /* if there are multiple variable names that match, use the shortest one
           * this seems to be what ARIA does in this case
           */
          best_len = define.variable.length();
          out_define = define;
        }
    }
  return (best_len != string::npos);
}

bool
Loader::preprocess_file (const std::string& filename, vector<LineInfo>& lines, int level, const std::string& content_str)
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

  LineInfo line_info;
  line_info.filename = filename;
  line_info.number = 1;

  size_t i = 0;
  while (i < contents.size())
    {
      char ch = contents[i];
      char next = i + 1 < contents.size() ? contents[i + 1] : 0;

      static const regex define_re ("#define\\s+(\\$\\S+)\\s+(\\S+)(.*)");
      static const regex include_re ("#include\\s*(?:=\\s*|\\s+)\"([^\"]*)\"(.*)");
      std::smatch sm;
      Control::Define define;

      if (ch == '/' && next == '*') /* block comment */
        {
          lines.push_back (line_info);
          line_info.line = "";

          size_t j = i, end = 0;
          while (j + 1 < contents.size()) /* search end of comment */
            {
              if (contents[j] == '\n')
                line_info.number++;

              if (contents[j] == '*' && contents[j + 1] == '/')
                {
                  end = j + 2;
                  break;
                }
              j++;
            }
          if (!end)
            {
              synth_->error ("%s unterminated block comment\n", line_info.location().c_str());
              return false;
            }
          i = end;
        }
      else if (ch == '/' && next == '/') /* line comment */
        {
          string line = line_lookahead (contents, i);
          i += line.size();
        }
      else if (ch == '#')
        {
          string line = line_lookahead (contents, i);
          if (regex_match (line, sm, define_re))
            {
              bool overwrite = false;
              for (auto& old_def : control.defines)
                {
                  if (old_def.variable == sm[1])
                    {
                      old_def.value = strip_spaces (sm[2]);
                      overwrite = true;
                    }
                }
              if (!overwrite)
                {
                  Control::Define define;
                  define.variable = sm[1];
                  define.value    = strip_spaces (sm[2]);
                  control.defines.push_back (define);
                }

              i += sm.length() - sm[3].length();
            }
          else if (regex_match (line, sm, include_re))
            {
              /* if there is text before the #include statement, this needs to
               * written to lines to preserve the order of the opcodes
               *
               * we don't bump the line number to allow text after the include
               * to be on the same line
               */
              lines.push_back (line_info);
              line_info.line = "";

              string include_filename = path_resolve_case_insensitive (path_absolute (path_join (sample_path, sm[1].str())));

              if (level < MAX_INCLUDE_DEPTH) // prevent infinite recursion for buggy .sfz
                {
                  bool inc_ok = preprocess_file (include_filename, lines, level + 1);
                  if (!inc_ok)
                    {
                      synth_->error ("%s unable to read #include '%s'\n", line_info.location().c_str(), include_filename.c_str());
                      return false;
                    }
                }
              else
                {
                  synth_->error ("%s exceeded maximum include depth (%d) while processing #include '%s'\n",
                                 line_info.location().c_str(), MAX_INCLUDE_DEPTH, include_filename.c_str());
                  return false;
                }
              i += sm.length() - sm[2].length();
            }
          else
            {
              /* just a normal '#' without #include or #define */
              line_info.line += ch;
              i++;
            }
        }
      else if (ch == '$')
        {
          string line = line_lookahead (contents, i);
          if (find_variable (line, define))
            {
              line_info.line += define.value;
              i += define.variable.size();
            }
          else
            {
              /* just a normal '$' without variable */
              line_info.line += ch;
              i++;
            }
        }
      else if (ch == '\r')
        {
          i++; // ignore
        }
      else if (ch == '\n')
        {
          lines.push_back (line_info);
          line_info.number++;
          line_info.line = "";
          i++;
        }
      else
        {
          line_info.line += ch;
          i++;
        }
    }
  if (!line_info.line.empty())
    {
      lines.push_back (line_info);
    }
  return true;
}

void
Loader::convert_lfo (Region& region, SimpleLFO& simple_lfo, SimpleLFO::Type type)
{
  int id = find_unused_lfo_id (region);
  int l = lfo_index_by_id (region, id);

  region.lfos[l].freq = simple_lfo.freq;
  region.lfos[l].fade = simple_lfo.fade;
  region.lfos[l].delay = simple_lfo.delay;
  region.lfos[l].freq_cc = simple_lfo.freq_cc;
  region.lfos[l].wave = 1; // sine

  switch (type)
  {
    case SimpleLFO::PITCH:
      region.lfos[l].pitch    = simple_lfo.depth;
      region.lfos[l].pitch_cc = simple_lfo.depth_cc;
      break;
    case SimpleLFO::AMP:
      region.lfos[l].volume    = simple_lfo.depth;
      region.lfos[l].volume_cc = simple_lfo.depth_cc;
      break;
    case SimpleLFO::FIL:
      region.lfos[l].cutoff    = simple_lfo.depth;
      region.lfos[l].cutoff_cc = simple_lfo.depth_cc;
      break;
  }
}

bool
Loader::parse (const string& filename, SampleCache& sample_cache, const vector<Control::Define>& defines)
{
  init_default_curves();

  sample_path = path_dirname (filename);

  // used by AriaBank files
  control.defines = defines;

  // read file
  vector<LineInfo> lines;

  HydrogenImport himport (synth_);
  if (himport.detect (filename))
    {
      string out;
      if (himport.parse (filename, out))
        {
          if (!preprocess_file (filename, lines, 0, out))
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
      if (!preprocess_file (filename, lines, 0))
        {
          synth_->error ("error reading file '%s'\n", filename.c_str());
          return false;
        }
    }

  static const regex space_re ("\\s+(.*)");
  static const regex tag_re ("<([^>]*)>(.*)");
  static const regex key_val_re ("([a-z0-9_]+)\\s*=\\s*(\\S+)(.*)");
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
              static const regex key_val_space_re_eol ("([a-z0-9_]+)\\s*=([^=<]+)");
              static const regex key_val_space_re_tag ("([a-z0-9_]+)\\s*=([^=<]+)(<.*)");
              static const regex key_val_space_re_eq ("([a-z0-9_]+)\\s*=([^=<]+)(\\s[a-z0-9_]+\\s*=.*)");

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

  // finalize curves
  for (auto& c : curves)
    curve_table.expand_curve (c);

  // do we have default behaviour for cc7 / cc10 or user defined behaviour?
  bool volume_cc7 = true;
  bool pan_cc10 = true;
  for (const auto& cc_info : cc_list)
    {
      if (cc_info.cc == 7)
        volume_cc7 = false;
      if (cc_info.cc == 10)
        pan_cc10 = false;
    }
  if (volume_cc7)
    {
      SetCC& set_cc = update_set_cc (7, 100);

      CCInfo cc7_info;
      cc7_info.cc = set_cc.cc;
      cc7_info.has_label = true;
      cc7_info.label = "Volume";
      cc7_info.default_value = set_cc.value;
      cc_list.push_back (cc7_info);
    }
  if (pan_cc10)
    {
      SetCC& set_cc = update_set_cc (10, 64);

      CCInfo cc10_info;
      cc10_info.cc = set_cc.cc;
      cc10_info.has_label = true;
      cc10_info.label = "Pan";
      cc10_info.default_value = set_cc.value;
      cc_list.push_back (cc10_info);
    }

  synth_->progress (0);
  for (size_t i = 0; i < regions.size(); i++)
    {
      Region& region = regions[i];

      uint max_offset = region.offset + region.offset_random + lrint (get_cc_vec_max (region.offset_cc));

      const auto load_result = sample_cache.load (region.sample, synth_->preload_time(), max_offset);
      region.cached_sample = load_result.sample;

      if (region.cached_sample)
        {
          // ensure that the life-time of our preload settings is the same as the life-time of this region
          //
          // this also allows having different preload settings for different regions / synth instances
          // on the same cached sample
          region.preload_info = load_result.preload_info;
        }

      if (!region.cached_sample)
        synth_->warning ("%s: missing sample: '%s'\n", filename.c_str(), region.sample.c_str());

      if (region.cached_sample && region.cached_sample->loop())
        {
          /* if values have been given explicitely, keep them
           *   -> user can override loop settings from wav file (cached sample)
           */
          if (!region.have_loop_mode)
            region.loop_mode = LoopMode::CONTINUOUS;

          if (!region.have_loop_start)
            region.loop_start = region.cached_sample->loop_start();

          if (!region.have_loop_end)
            region.loop_end = region.cached_sample->loop_end();
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

      region.volume_cc7 = volume_cc7;
      region.pan_cc10   = pan_cc10;

      /* update progress info */
      synth_->progress ((i + 1) * 100.0 / regions.size());
    }
  // generate final key_list
  for (const auto& [key, key_info] : key_map)
    key_list.push_back (key_info);

  // generate final cc_list
  std::sort (cc_list.begin(), cc_list.end(),
    [] (const CCInfo& a, const CCInfo& b) {
      return a.cc < b.cc;
    });

  for (auto& region : regions)
    {
      // convert SFZ1 lfos to SFZ2 lfos
      if (region.pitchlfo.used)
        convert_lfo (region, region.pitchlfo, SimpleLFO::PITCH);

      if (region.amplfo.used)
        convert_lfo (region, region.amplfo, SimpleLFO::AMP);

      if (region.fillfo.used)
        convert_lfo (region, region.fillfo, SimpleLFO::FIL);

      // find memory requirements for lfos
      limits.max_lfos = std::max (limits.max_lfos, region.lfos.size());

      size_t lfo_mods = 0;
      for (const auto& lfo : region.lfos)
        lfo_mods += lfo.lfo_mods.size();

      limits.max_lfo_mods = std::max (limits.max_lfo_mods, lfo_mods);
    }

  synth_->debug ("*** limits: max_lfos=%zd max_lfo_mods=%zd\n", limits.max_lfos, limits.max_lfo_mods);
  synth_->debug ("*** regions: %zd\n", regions.size());
  return true;
}

float
Loader::get_cc_curve_max (const CCParamVec::Entry& entry)
{
  int curvecc = entry.curvecc;
  if (curvecc >= 0 && curvecc < int (curves.size()))
    {
      if (!curves[curvecc].empty())
        {
          /* find maximum factor for the curve */
          float max_factor = 0;

          for (int cc_value = 0; cc_value < 128; cc_value++)
            max_factor = std::max (max_factor, curves[entry.curvecc].get (cc_value));

          return max_factor;
        }
    }
  return 1;
}

float
Loader::get_cc_vec_max (const CCParamVec& cc_param_vec)
{
  float max_value = 0;
  for (const auto& entry : cc_param_vec)
    max_value += get_cc_curve_max (entry) * entry.value;

  return max_value;
}
