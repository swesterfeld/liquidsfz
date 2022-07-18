// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

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
