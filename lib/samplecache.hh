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
#include <atomic>
#include <thread>

#include <sndfile.h>
#include <unistd.h>

namespace LiquidSFZInternal
{

struct SampleBuffer
{
  static constexpr size_t frames_per_buffer = 1000;

  std::atomic<int>   loaded = 0;
  std::vector<short> samples;
};

class SampleCache
{
public:
  struct Entry {
    bool                        loop = false;
    int                         loop_start = 0;
    int                         loop_end = 0;

    std::vector<std::unique_ptr<SampleBuffer>>   buffers; /* FIXME: what about same sample, different settings */
    uint                        sample_rate;
    uint                        channels;
    size_t                      n_samples = 0;

    std::atomic<int>            playback_count = 0;
    std::atomic<int>            max_buffer_index = 0;
    bool                        loaded = false;

    std::string                 filename;

    void
    update_max_buffer_index (int value)
    {
      int prev_max_index = max_buffer_index;
      while (prev_max_index < value && !max_buffer_index.compare_exchange_weak (prev_max_index, value))
        ;
    }
    short
    get (size_t pos)
    {
      size_t buffer_index = pos / (SampleBuffer::frames_per_buffer * channels);
      if (buffer_index < buffers.size())
        {
          auto& buffer = *buffers[buffer_index];
          if (buffer.loaded)
            {
              update_max_buffer_index (buffer_index);

              size_t sample_index = pos - (buffer_index * (SampleBuffer::frames_per_buffer * channels));
              if (sample_index < buffer.samples.size())
                return buffer.samples[sample_index];
            }
        }
      return 0;
    }

    void
    start_playback()
    {
      playback_count++;
    }

    void
    end_playback()
    {
      playback_count--;
    }
  };
private:
  std::map<std::string, std::weak_ptr<Entry>> cache;
  std::mutex mutex;
  std::thread loader_thread;

  bool quit_background_loader = false;

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
    new_entry->sample_rate = sfinfo.samplerate;
    new_entry->channels = sfinfo.channels;
    new_entry->n_samples = sfinfo.frames * sfinfo.channels;
    new_entry->filename = filename;

    /* preload sample data */
    sf_count_t pos = 0;
    size_t b = 0;
    while (pos < sfinfo.frames)
      {
        new_entry->buffers.emplace_back (std::make_unique<SampleBuffer>());

        if (b < 20)
          load_buffer (new_entry, sndfile, sfinfo, b++);

        pos += SampleBuffer::frames_per_buffer;
      }
    error = sf_close (sndfile);
    if (error)
      {
        printf ("error during close\n");
        return nullptr;
      }

    cache[filename] = new_entry;
    return new_entry;
  }
  void
  load_buffer (std::shared_ptr<Entry> entry, SNDFILE *sndfile, const SF_INFO& sfinfo, size_t b)
  {
    auto& buffer = *entry->buffers[b];
    if (buffer.loaded == 0)
      {
        sf_seek (sndfile, b * SampleBuffer::frames_per_buffer, SEEK_SET);

        std::vector<short> isamples (SampleBuffer::frames_per_buffer * entry->channels);

        sf_count_t count;

        int mask_format = sfinfo.format & SF_FORMAT_SUBMASK;
        if (mask_format == SF_FORMAT_FLOAT || mask_format == SF_FORMAT_DOUBLE)
          {
            // https://github.com/erikd/libsndfile/issues/388
            //
            // for floating point wav files, libsndfile isn't able to convert to shorts
            // properly when using sf_readf_short(), so we convert the data manually

            std::vector<float> fsamples (isamples.size());
            count = sf_readf_float (sndfile, &fsamples[0], isamples.size());

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
            count = sf_readf_short (sndfile, &isamples[0], SampleBuffer::frames_per_buffer);
          }
        if (count > 0)
          {
            isamples.resize (count * sfinfo.channels);

            buffer.samples = std::move (isamples);
          }
        /*
         * we also set this to one if loading failed (short read) because
         * otherwise we would reload the same buffer again and again
         */
        buffer.loaded = 1;
      }
  }
  void
  load (std::shared_ptr<Entry> entry)
  {
    for (size_t b = 0; b < entry->buffers.size(); b++)
      {
        if (entry->buffers[b]->loaded == 0 && b < size_t (entry->max_buffer_index.load() + 20))
          {
            SF_INFO sfinfo = { 0, };
            SNDFILE *sndfile = sf_open (entry->filename.c_str(), SFM_READ, &sfinfo);

            if (sndfile)
              {
                printf ("loading %s / buffer %zd / %s\n", entry->filename.c_str(), b, cache_stats().c_str());
                load_buffer (entry, sndfile, sfinfo, b);
                cache_stats();
                sf_close (sndfile);
              }
          }
      }
  }
  std::string
  cache_stats()
  {
    size_t bytes = 0;
    size_t entries = 0;
    for (auto it : cache)
      {
        auto entry = it.second.lock();
        if (entry)
          {
            for (auto& buffer : entry->buffers)
              bytes += buffer->samples.size() * sizeof (buffer->samples[0]);

            entries++;
          }
      }
    return string_printf ("cache holds %.2f MB in %zd entries", bytes / 1024. / 1024., entries);
  }

  SampleCache()
  {
    loader_thread = std::thread (&SampleCache::background_loader, this);
  }
  ~SampleCache()
  {
    {
      std::lock_guard lg (mutex);
      quit_background_loader = true;
    }
    loader_thread.join();
  }

  void
  background_loader()
  {
    while (!quit_background_loader)
      {
        {
          std::lock_guard lg (mutex);
          for (auto it : cache)
            {
              auto entry = it.second.lock();
              if (entry)
                {
                  if (entry->playback_count && !entry->loaded)
                    {
                      load (entry);
                    }
                }
            }
        }
        usleep (20 * 1000);
      }
  }
};

}

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
