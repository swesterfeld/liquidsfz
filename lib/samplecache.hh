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
#include <cassert>

#include <sndfile.h>
#include <unistd.h>

namespace LiquidSFZInternal
{

struct SampleBuffer
{
  static constexpr size_t frames_per_buffer = 1000;

  struct Data
  {
    std::vector<float> samples;
    std::atomic<int>   used;
  };

  std::atomic<int>    loaded = 0;
  std::atomic<Data *> data = nullptr;
};

class SampleBufferVector
{
  size_t                      size_    = 0;
  std::atomic<SampleBuffer *> buffers_ = nullptr;
public:
  SampleBufferVector()
  {
    static_assert (decltype (buffers_)::is_always_lock_free);
  }
  ~SampleBufferVector()
  {
    if (size_ || buffers_)
      fprintf (stderr, "liquidsfz: SampleBufferVector: should clear the vector before deleting\n");
  }
  SampleBufferVector (const SampleBufferVector&) = delete;
  SampleBufferVector& operator=  (const SampleBufferVector&) = delete;

  size_t
  size()
  {
    return size_;
  }
  SampleBuffer&
  operator[] (size_t pos)
  {
    return buffers_[pos];
  }
  void
  resize (size_t size)
  {
    assert (size_ == 0);
    assert (buffers_ == nullptr);
    size_ = size;
    buffers_ = new SampleBuffer[size];
  }
  auto
  take_atomically (SampleBufferVector& other)
  {
    auto free_function = [buffers = buffers_.load(), size = size_] () {
      for (size_t b = 0; b < size; b++)
        {
          auto data = buffers[b].data.load();
          if (data)
            data->used--;
        }
      if (buffers)
        delete[] buffers;
    };

    assert (size_ == other.size_);

    buffers_ = other.buffers_.load();
    for (size_t b = 0; b < size_; b++)
      if (buffers_[b].data)
        buffers_[b].data.load()->used++;

    other.buffers_ = nullptr;
    other.size_ = 0;
    return free_function;
  }
  void
  clear()
  {
    if (buffers_)
      {
        for (size_t b = 0; b < size_; b++)
          {
            auto data = buffers_[b].data.load();
            if (data)
              data->used--;
          }
        delete[] buffers_;
      }
    size_ = 0;
    buffers_ = nullptr;
  }
};

class SampleCache
{
public:
  struct Entry {
    bool                        loop = false;
    int                         loop_start = 0;
    int                         loop_end = 0;

    SampleBufferVector          buffers;
    uint                        sample_rate;
    uint                        channels;
    size_t                      n_samples = 0;

    std::atomic<int>            playback_count = 0;
    std::atomic<int>            max_buffer_index = 0;
    bool                        playing = false;

    std::string                 filename;

    std::vector<std::function<void()>> free_functions;

    void
    update_max_buffer_index (int value)
    {
      int prev_max_index = max_buffer_index;
      while (prev_max_index < value && !max_buffer_index.compare_exchange_weak (prev_max_index, value))
        ;
    }
    float
    get (size_t pos)
    {
      size_t buffer_index = pos / (SampleBuffer::frames_per_buffer * channels);
      if (buffer_index < buffers.size())
        {
          auto& buffer = buffers[buffer_index];
          if (buffer.loaded)
            {
              update_max_buffer_index (buffer_index);

              size_t sample_index = pos - (buffer_index * (SampleBuffer::frames_per_buffer * channels));

              const SampleBuffer::Data *data = buffer.data.load();
              if (data && sample_index < data->samples.size())
                return data->samples[sample_index];
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
  std::vector<SampleBuffer::Data *> data_entries;

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
  void
  remove_unused_data_entries()
  {
    auto is_unused = [] (SampleBuffer::Data *data) {
      return data->used == 0;
    };
    data_entries.erase (std::remove_if (data_entries.begin(), data_entries.end(), is_unused), data_entries.end());
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
    size_t n_buffers = 0;
    while (pos < sfinfo.frames)
      {
        n_buffers++;
        pos += SampleBuffer::frames_per_buffer;
      }
    new_entry->buffers.resize (n_buffers);
    for (size_t b = 0; b < n_buffers; b++)
      {
        if (b < 20)
          load_buffer (new_entry, sndfile, b);
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
  load_buffer (std::shared_ptr<Entry> entry, SNDFILE *sndfile, size_t b)
  {
    auto& buffer = entry->buffers[b];
    if (buffer.loaded == 0)
      {
        sf_seek (sndfile, b * SampleBuffer::frames_per_buffer, SEEK_SET);

        std::vector<float> fsamples (SampleBuffer::frames_per_buffer * entry->channels);

        sf_count_t count = sf_readf_float (sndfile, &fsamples[0], fsamples.size() / entry->channels);
        if (count > 0)
          {
            fsamples.resize (count * entry->channels);

            auto data = new SampleBuffer::Data();
            data->samples = std::move (fsamples);
            data->used = 1;

            buffer.data = data;

            data_entries.push_back (data);
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
        if (entry->buffers[b].loaded == 0 && b < size_t (entry->max_buffer_index.load() + 20))
          {
            SF_INFO sfinfo = { 0, };
            SNDFILE *sndfile = sf_open (entry->filename.c_str(), SFM_READ, &sfinfo);

            if (sndfile)
              {
                printf ("loading %s / buffer %zd\n", entry->filename.c_str(), b);
                load_buffer (entry, sndfile, b);
                sf_close (sndfile);
              }
          }
      }
  }
  std::string
  cache_stats()
  {
    size_t bytes = 0;
    for (auto data : data_entries)
      {
        if (data)
          bytes += data->samples.size() * sizeof (data->samples[0]);
      }
    return string_printf ("cache holds %.2f MB in %zd entries", bytes / 1024. / 1024., cache.size());
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

    for (auto it : cache)
      {
        auto entry = it.second.lock();
        if (entry)
          {
            for (auto func : entry->free_functions)
              func();

            entry->free_functions.clear();
            entry->buffers.clear();
          }
      }

    remove_unused_data_entries();
    remove_expired_entries();

    printf ("cache stats in SampleCache destructor: %s\n", cache_stats().c_str());
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
                  if (entry->playback_count)
                    {
                      load (entry);
                      entry->playing = true;
                    }
                  if (!entry->playback_count && entry->playing)
                    {
                      SampleBufferVector new_buffers;
                      new_buffers.resize (entry->buffers.size());
                      for (size_t b = 0; b < entry->buffers.size(); b++)
                        {
                          if (b > 20 && entry->buffers[b].loaded)
                            {
                              new_buffers[b].loaded = 0;
                              new_buffers[b].data   = nullptr;
                            }
                          else
                            {
                              new_buffers[b].loaded = entry->buffers[b].loaded.load();
                              new_buffers[b].data   = entry->buffers[b].data.load();
                            }
                        }
                      auto free_function = entry->buffers.take_atomically (new_buffers);
                      entry->free_functions.push_back (free_function);
                      entry->max_buffer_index = 0;
                      entry->playing = false;

                      printf ("unloaded %s / %s\n", entry->filename.c_str(), cache_stats().c_str());
                    }
                  if (!entry->playback_count)
                    {
                      /*
                       * we can safely free the old data structures here because there cannot
                       * be any threads that are reading an old version of the sample buffer vector
                       * at this point
                       */
                      for (auto func : entry->free_functions)
                        func();

                      entry->free_functions.clear();
                    }
                }
            }
          remove_unused_data_entries();
        }
        usleep (20 * 1000);
      }
  }
};

}

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
