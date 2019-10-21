/*
 * liquidsfz - sfz support using fluidsynth
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

#include <algorithm>

using std::string;

void
Loader::set_key_value (const string& key, const string& value)
{
  if (region_type == RegionType::NONE)
    return;

  Region& region = (region_type == RegionType::REGION) ? active_region : active_group;
  printf ("+++ '%s' = '%s'\n", key.c_str(), value.c_str());
  if (key == "sample")
    {
      // on unix, convert \-seperated filename to /-separated filename
      string native_filename = value;
      std::replace (native_filename.begin(), native_filename.end(), '\\', fs::path::preferred_separator);

      region.sample = fs::absolute (sample_path / native_filename);
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
  else if (key == "lorand")
    region.lorand = convert_float (value);
  else if (key == "hirand")
    region.hirand = convert_float (value);
  else if (key == "loop_start")
    region.loop_start = convert_int (value);
  else if (key == "loop_end")
    region.loop_end = convert_int (value);
  else if (starts_with (key, "locc"))
    {
      int cc = convert_int (key.substr (4));
      if (cc >= 0 && cc <= 127)
        region.locc[cc] = convert_int (value);
    }
  else if (starts_with (key, "hicc"))
    {
      int cc = convert_int (key.substr (4));
      if (cc >= 0 && cc <= 127)
        region.hicc[cc] = convert_int (value);
    }
  else if (starts_with (key, "on_locc") || starts_with (key, "on_hicc"))
    region.trigger = Trigger::CC;
  else if (key == "trigger")
    region.trigger = convert_trigger (value);
  else if (key == "seq_length")
    region.seq_length = convert_int (value);
  else if (key == "seq_position")
    region.seq_position = convert_int (value);
  else if (key == "ampeg_delay")
    region.ampeg_delay = convert_float (value);
  else if (key == "ampeg_attack")
    region.ampeg_attack = convert_float (value);
  else if (key == "ampeg_decay")
    region.ampeg_decay = convert_float (value);
  else if (key == "ampeg_sustain")
    region.ampeg_sustain = convert_float (value);
  else if (key == "ampeg_release")
    region.ampeg_release = convert_float (value);
  else
    printf ("unsupported opcode '%s'\n", key.c_str());
}

