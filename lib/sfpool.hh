// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#ifndef LIQUIDSFZ_SFPOOL_HH
#define LIQUIDSFZ_SFPOOL_HH

#include <sndfile.h>

#include <string>
#include <memory>
#include <map>

#include "utils.hh"

namespace LiquidSFZInternal
{

class SFPool
{
public:
  /* to support virtual io read from memory */
  struct MappedVirtualData
  {
    unsigned char *mem    = nullptr;
    sf_count_t     size   = 0;
    sf_count_t     offset = 0;
    SF_VIRTUAL_IO  io;
  };

  class Entry
  {
    sf_count_t  position_ = 0;
  public:
    SNDFILE    *sndfile = nullptr;
    SF_INFO     sfinfo = { 0, };
    std::string filename;
    double      time = 0;

    MappedVirtualData mapped_data; // for mmap

    sf_count_t seek_read_frames (sf_count_t pos, float *buffer, sf_count_t frame_count);

    ~Entry();
  };
  typedef std::shared_ptr<Entry> EntryP;

  static constexpr size_t max_fds  = 64;
  static constexpr double max_time = 30; /* seconds */

#if LIQUIDSFZ_64BIT
  static constexpr bool   use_mmap = true;
#else
  static constexpr bool   use_mmap = false;
#endif

private:
  std::map<std::string, EntryP> cache;

  SNDFILE *mmap_open (const std::string& filename, SF_INFO *sfinfo, EntryP entry);
public:
  EntryP open (const std::string& filename, SF_INFO *sfinfo);
  void cleanup();

};

}

#endif /* LIQUIDSFZ_SFPOOL_HH */
