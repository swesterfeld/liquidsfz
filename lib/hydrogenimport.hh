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

#ifndef LIQUIDSFZ_HYDROGEN_HH
#define LIQUIDSFZ_HYDROGEN_HH

#include <string>

#include "synth.hh"
#include "pugixml.hh"

namespace LiquidSFZInternal
{

class HydrogenImport
{
  struct DrumkitComponent
  {
    int    id = 0;
    double volume = 1;
  };
  struct Region
  {
    std::string sample;
    int lovel = 0;
    int hivel = 0;
    double layer_gain = 1;
    double pitch = 0;
  };

  Synth *synth_ = nullptr;

  void cleanup_regions (std::vector<Region>& regions);
  void add_layer (const pugi::xml_node& layer, std::vector<Region>& regions);

public:
  HydrogenImport (Synth *synth);

  bool detect (const std::string& filename);
  bool parse (const std::string& filename, std::string& out);
};

}

#endif /* LIQUIDSFZ_HYDROGEN_HH */
