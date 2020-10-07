/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2020  Stefan Westerfeld
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


#include "hydrogenimport.hh"
#include "pugixml.hh"

#include <math.h>

#include <vector>

namespace LiquidSFZInternal {

using pugi::xml_document;
using pugi::xml_node;

using std::vector;
using std::string;

struct Region
{
  string sample;
  int lovel = 0;
  int hivel = 0;
};

void
cleanup_regions (vector<Region>& regions)
{
  vector<Region *> note_region (128);

  /* first step: find and assign exactly one region for each midi note */
  for (int note = 1; note <= 127; note++)
    {
      for (auto& r : regions)
        {
          if (r.lovel <= note && r.hivel >= note)
            {
              /* simple case: note is within the region range */
              note_region[note] = &r;
              break;
            }
        }
      if (note_region[note] == nullptr) /* not assigned yet */
        {
          /* try all regions, assign note to region with closest lovel */
          int best_dist = 128;
          for (auto& r : regions)
            {
              int dist = abs (note - r.lovel);
              if (dist < best_dist)
                {
                  best_dist = dist;
                  note_region[note] = &r;
                }
            }
        }
    }
  /* generate new lovel / hivel from matching notes */
  for (auto& r : regions)
    {
      int lovel = 128, hivel = 0;

      for (int note = 1; note <= 127; note++)
        {
          if (note_region[note] == &r)
            {
              if (note < lovel)
                lovel = note;
              if (note > hivel)
                hivel = note;
            }
        }
      r.lovel = lovel;
      r.hivel = hivel;
    }
}

bool
HydrogenImport::parse (const string& filename)
{
  constexpr bool use_midi_out_note = false;

  xml_document doc;
  auto result = doc.load_file (filename.c_str());
  if (!result)
    {
      printf ("load error\n");
      return 1;
    }
  xml_node drumkit_info = doc.child ("drumkit_info");
  xml_node instrument_list = drumkit_info.child ("instrumentList");
  int instrument_index = 0;
  int region_count = 0;
  int group = 1;
  for (xml_node instrument : instrument_list.children ("instrument"))
    {
      xml_node name = instrument.child ("name");
      printf ("// %s\n", name.text().as_string());
      int key = instrument_index + 36;

      int midi_out_note = instrument.child ("midiOutNote").text().as_int();
      if (use_midi_out_note && midi_out_note > 0)
        key = midi_out_note;

      printf ("<group>\n");
      printf ("  key=%d\n", key);
      printf ("  loop_mode=one_shot\n");
      printf ("  amp_velcurve_1=0.008\n");
      printf ("  group=%d\n", group);
      printf ("  off_by=%d\n", group);
      printf ("\n");

      vector<Region> regions;
      auto do_layer = [&region_count,&regions] (const xml_node& layer)
        {
          xml_node filename = layer.child ("filename");
#if 0
          printf (" - %s\n", filename.text().as_string());
          printf (" min=%f\n", layer.child ("min").text().as_double());
          printf (" max=%f\n", layer.child ("max").text().as_double());
#endif
          string sample = filename.text().as_string();
          int lovel = lrint (layer.child ("min").text().as_double() * 127);
          int hivel = lrint (layer.child ("max").text().as_double() * 127);
          regions.push_back ({ sample, lovel, hivel });
        };
      // new style: layer is in an instrumentComponent node
      xml_node instrument_component = instrument.child ("instrumentComponent");
      for (xml_node layer : instrument_component.children ("layer"))
        do_layer (layer);

      // old style: layer is a child of the instrument
      for (xml_node layer : instrument.children ("layer"))
        do_layer (layer);

      cleanup_regions (regions);

      for (auto r : regions)
        {
          printf ("  <region>\n");
          printf ("    lovel=%d hivel=%d\n", r.lovel, r.hivel);
          printf ("    sample=%s\n", r.sample.c_str());
          printf ("\n");

          region_count++;
        }

      // even older style: filename is child of this node
      xml_node filename = instrument.child ("filename");
      if (filename)
        {
          string sample = filename.text().as_string();

          printf ("  <region>\n");
          printf ("    sample=%s\n", sample.c_str());
          printf ("\n");

          region_count++;
        }
      printf ("\n");

      group++;
      instrument_index++;
    }

  if (region_count == 0)
    {
      printf ("load error: no hydrogen regions found in input file\n");
      return 1;
    }
  return true;
}

}
