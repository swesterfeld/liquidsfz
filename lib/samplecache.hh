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

#ifndef LIQUIDSFZ_SAMPLECACHE_HH
#define LIQUIDSFZ_SAMPLECACHE_HH

#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <mutex>

#include <sndfile.h>

namespace LiquidSFZInternal
{

class SampleCache
{
public:
  struct Entry {
    bool                loop = false;
    int                 loop_start = 0;
    int                 loop_end = 0;

    std::vector<short>  samples; /* FIXME: what about same sample, different settings */
    uint                sample_rate;
    uint                channels;
  };
private:
  std::map<std::string, std::weak_ptr<Entry>> cache;
  std::mutex mutex;

protected:
  void
  remove_expired_entries()
  {
    /* the deletion of the actual cache entry is done automatically (shared ptr)
     * this just removes entries in the std::map for filenames with a null weak ptr
     */
    auto it = cache.begin();
    while (it != cache.end())
      {
        if (it->second.expired())
          {
            it = cache.erase (it);
          }
        else
          {
            it++;
          }
      }
  }
public:
  std::shared_ptr<Entry>
  load (const std::string& filename)
  {
    std::lock_guard lg (mutex);

    std::shared_ptr<Entry> cached_entry = cache[filename].lock();
    if (cached_entry) /* already in cache? -> nothing to do */
      return cached_entry;

    remove_expired_entries();

    auto new_entry = std::make_shared<Entry>();

    SF_INFO sfinfo = { 0, };
    SNDFILE *sndfile = sf_open (filename.c_str(), SFM_READ, &sfinfo);

    int error = sf_error (sndfile);
    if (error)
      {
        // sf_strerror (sndfile)
        if (sndfile)
          sf_close (sndfile);

        return nullptr;
      }

    /* load loop points */
    SF_INSTRUMENT instrument = {0,};
    if (sf_command (sndfile, SFC_GET_INSTRUMENT, &instrument, sizeof (instrument)) == SF_TRUE)
      {
        if (instrument.loop_count)
          {
            if (instrument.loops[0].mode == SF_LOOP_FORWARD)
              {
                new_entry->loop = true;
                new_entry->loop_start = instrument.loops[0].start;
                new_entry->loop_end = instrument.loops[0].end;
              }
          }
        }
    /* load sample data */
    std::vector<short> isamples (sfinfo.frames * sfinfo.channels);
    sf_count_t count;

    int mask_format = sfinfo.format & SF_FORMAT_SUBMASK;
    if (mask_format == SF_FORMAT_FLOAT || mask_format == SF_FORMAT_DOUBLE)
      {
        // https://github.com/erikd/libsndfile/issues/388
        //
        // for floating point wav files, libsndfile isn't able to convert to shorts
        // properly when using sf_readf_short(), so we convert the data manually

        std::vector<float> fsamples (sfinfo.frames * sfinfo.channels);
        count = sf_readf_float (sndfile, &fsamples[0], sfinfo.frames);

        for (size_t i = 0; i < fsamples.size(); i++)
          {
            const double norm      =  0x8000;
            const double min_value = -0x8000;
            const double max_value =  0x7FFF;

            isamples[i] = lrint (std::clamp (fsamples[i] * norm, min_value, max_value));
          }
      }
    else
      {
        count = sf_readf_short (sndfile, &isamples[0], sfinfo.frames);
      }
    if (count != sfinfo.frames)
      {
        printf ("short read\n");
        return nullptr;
      }
    error = sf_close (sndfile);
    if (error)
      {
        printf ("error during close\n");
        return nullptr;
      }

    new_entry->samples = std::move (isamples);
    new_entry->sample_rate = sfinfo.samplerate;
    new_entry->channels = sfinfo.channels;

    cache[filename] = new_entry;
    return new_entry;
  }
};

}

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
