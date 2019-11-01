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

#ifndef LIQUIDSFZ_SAMPLECACHE_HH
#define LIQUIDSFZ_SAMPLECACHE_HH

#include <vector>
#include <map>
#include <memory>

#include <sndfile.h>

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
  std::map<std::string, std::unique_ptr<Entry>> cache;
public:
  Entry *
  load (const std::string& filename)
  {
    if (cache[filename]) /* already in cache? -> nothing to do */
      return cache[filename].get();

    auto new_entry = std::make_unique<Entry>();

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
    sf_count_t count = sf_readf_short (sndfile, &isamples[0], sfinfo.frames);
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

    cache[filename] = std::move (new_entry);
    return cache[filename].get();
  }
};

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
