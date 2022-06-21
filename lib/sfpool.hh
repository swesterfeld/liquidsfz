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

namespace LiquidSFZInternal
{

class SFPool
{
public:
  struct Entry {
    SNDFILE *sndfile = nullptr;
    std::string filename;
    double time = 0;

    ~Entry()
    {
      if (sndfile)
        {
          printf ("sf_close %s\n", filename.c_str());
          sf_close (sndfile);
        }
    }
  };
  typedef std::shared_ptr<Entry> EntryP;

  static constexpr size_t max_fds  = 64;
  static constexpr double max_time = 30; /* seconds */

private:
  std::map<std::string, EntryP> cache;

public:
  EntryP
  open (const std::string& filename)
  {
    EntryP entry = cache[filename];
    if (entry)
      {
        entry->time = get_time();
        return entry;
      }

    SF_INFO sf_info = { 0, };

    entry = std::make_shared<Entry>();
    entry->filename = filename;
    entry->sndfile = sf_open (filename.c_str(), SFM_READ, &sf_info);
    entry->time = get_time();
    cache[filename] = entry;
    printf ("sf_open %s -> %p\n", filename.c_str(), entry->sndfile);

    cleanup(); // close old files if any
    return entry;
  }
  void
  cleanup()
  {
    auto close_candidates = [&]() {
      std::vector<EntryP> close_candidates;

      for (auto [ filename, entry ] : cache)
        close_candidates.push_back (entry);
      std::sort (close_candidates.begin(), close_candidates.end(), [] (auto& a, auto& b) { return a->time < b->time; });

      return close_candidates;
    };

    /* enforce time limit */
    double now = get_time();
    for (auto cc : close_candidates())
      {
        if (fabs (cc->time - now) > max_time)
          cache.erase (cc->filename);
      }

    /* enforce max_fds limit */
    while (cache.size() > max_fds)
      {
        auto cc = close_candidates()[0];
        cache.erase (cc->filename);
      }
  }
};

}

#endif /* LIQUIDSFZ_SFPOOL_HH */
