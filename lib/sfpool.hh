/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2022  Stefan Westerfeld
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
