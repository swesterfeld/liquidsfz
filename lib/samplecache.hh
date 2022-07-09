/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019-2022  Stefan Westerfeld
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

#include "sfpool.hh"
#include "log.hh"

namespace LiquidSFZInternal
{

class SampleCache;

typedef sf_count_t sample_count_t;

struct SampleBuffer
{
  static constexpr sample_count_t frames_per_buffer = 1000;
  static constexpr sample_count_t frames_overlap = 64;

  class Data
  {
    SampleCache *sample_cache_ = nullptr;
    int          n_samples_ = 0;
    int          ref_count_ = 1;
  public:
    Data (SampleCache *sample_cache, size_t n_samples);
    ~Data();
    void
    ref()
    {
      ref_count_++;
    }
    void
    unref()
    {
      ref_count_--;
      if (ref_count_ == 0)
        delete this;
    }
    std::vector<float> samples;
    sample_count_t     start_n_values = 0;
  };

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
            data->unref();
        }
      if (buffers)
        delete[] buffers;
    };

    assert (size_ == other.size_);

    buffers_ = other.buffers_.load();
    for (size_t b = 0; b < size_; b++)
      if (buffers_[b].data)
        buffers_[b].data.load()->ref();

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
              data->unref();
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
    SampleCache                *sample_cache = nullptr;

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
    SFPool::EntryP              mmap_sf;

    void
    update_max_buffer_index (int value)
    {
      int prev_max_index = max_buffer_index;
      while (prev_max_index < value && !max_buffer_index.compare_exchange_weak (prev_max_index, value))
        ;
    }
    class PlayHandle
    {
    private:
      Entry           *entry_     = nullptr;
      bool             live_mode_ = false;

      const float     *samples_   = nullptr;
      sample_count_t   start_pos_ = 0;
      sample_count_t   end_pos_   = 0;

    public:
      PlayHandle (const PlayHandle& p)
      {
        start_playback (p.entry_, p.live_mode_);
      }
      PlayHandle&
      operator= (const PlayHandle& p)
      {
        start_playback (p.entry_, p.live_mode_);
        return *this;
      }
      PlayHandle()
      {
      }
      ~PlayHandle()
      {
        end_playback();
      }
      void
      start_playback (Entry *entry, bool live_mode)
      {
        if (entry != entry_)
          {
            if (entry_)
              entry_->end_playback();

            entry_ = entry;
            if (entry_)
              entry_->start_playback();

            samples_ = nullptr;
            start_pos_ = end_pos_ = 0;
          }
        live_mode_ = live_mode;
      }
      void
      end_playback()
      {
        start_playback (nullptr, live_mode_);
      }
      const float *
      get_n (sample_count_t pos, sample_count_t n)
      {
        /* usually this succeeds quickly */
        sample_count_t offset = pos - start_pos_;
        if (offset >= 0 && pos + n < end_pos_)
          return samples_ + offset;

        /* if not, we try to find a matching block */
        if (lookup (pos))
          {
            if (pos + n < end_pos_)
              return samples_ + pos - start_pos_;
          }
        /* finally we fail */
        return nullptr;
      }
      float
      get (sample_count_t pos)
      {
        const float *sample = get_n (pos, 1);

        return sample ? *sample : 0;
      }
    private:
      bool
      lookup (sample_count_t pos);
    };

    void
    start_playback()
    {
      playback_count++;
      sample_cache->playback_entries_need_update.store (true);
    }

    void
    end_playback()
    {
      playback_count--;
      sample_cache->playback_entries_need_update.store (true);
    }
  };
  typedef std::shared_ptr<SampleCache::Entry> EntryP;
private:
  std::map<std::string, std::weak_ptr<Entry>> cache;
  std::mutex mutex;
  std::thread loader_thread;
  std::atomic<size_t> atomic_n_total_bytes_ = 0;
  std::atomic<uint>   atomic_cache_file_count_ = 0;
  SFPool sf_pool;
  double last_cleanup_time = 0;
  std::vector<EntryP> playback_entries;
  std::atomic<bool>   playback_entries_need_update = false;

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
    atomic_cache_file_count_ = cache.size();
  }
public:
  EntryP
  load (const std::string& filename)
  {
    std::lock_guard lg (mutex);

    EntryP cached_entry = cache[filename].lock();
    if (cached_entry) /* already in cache? -> nothing to do */
      return cached_entry;

    remove_expired_entries();

    auto new_entry = std::make_shared<Entry>();

    SF_INFO sfinfo = { 0, };
    auto sf = sf_pool.open (filename, &sfinfo);
    SNDFILE *sndfile = sf->sndfile;

    if (!sndfile)
      return nullptr;

#if 0
    int error = sf_error (sndfile);
    if (error)
      {
        // sf_strerror (sndfile)
        if (sndfile)
          sf_close (sndfile);

        return nullptr;
      }
#endif

    /* if we use mmap, we keep the file open */
    if (SFPool::use_mmap)
      new_entry->mmap_sf = sf;

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
    new_entry->sample_cache = this;
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
          load_buffer (new_entry.get(), sndfile, b);
      }
    cache[filename] = new_entry;
    atomic_cache_file_count_ = cache.size();
    return new_entry;
  }
  void
  load_buffer (Entry *entry, SNDFILE *sndfile, size_t b)
  {
    auto& buffer = entry->buffers[b];
    if (!buffer.data)
      {
        auto data = new SampleBuffer::Data (this, (SampleBuffer::frames_per_buffer + SampleBuffer::frames_overlap) * entry->channels);
        data->start_n_values = (b * SampleBuffer::frames_per_buffer - SampleBuffer::frames_overlap) * entry->channels;

        // FIXME: may want to check return codes
        sf_seek (sndfile, b * SampleBuffer::frames_per_buffer, SEEK_SET);
        sf_readf_float (sndfile, &data->samples[SampleBuffer::frames_overlap * entry->channels], SampleBuffer::frames_per_buffer);

        if (b > 0)
          {
            // copy last samples from last buffer to first samples from this buffer to make buffers overlap
            const auto last_data = entry->buffers[b - 1].data.load();
            const float *from_samples = &last_data->samples[SampleBuffer::frames_per_buffer * entry->channels];
            std::copy_n (from_samples, SampleBuffer::frames_overlap * entry->channels, &data->samples[0]);
          }

        buffer.data = data;
      }
  }
  void
  load (Entry *entry)
  {
    for (size_t b = 0; b < entry->buffers.size(); b++)
      {
        if (!entry->buffers[b].data && b < size_t (entry->max_buffer_index.load() + 20))
          {
            SF_INFO sfinfo;
            auto sf = SFPool::use_mmap ? entry->mmap_sf : sf_pool.open (entry->filename, &sfinfo);

            if (sf->sndfile)
              {
                //printf ("loading %s / buffer %zd\n", entry->filename.c_str(), b);
                load_buffer (entry, sf->sndfile, b);
              }
          }
      }
  }
  void
  load_data_for_playback_entries()
  {
    if (playback_entries_need_update.load())
      {
        playback_entries_need_update.store (false);

        playback_entries.clear();
        for (const auto& [key, value] : cache)
          {
            auto entry = value.lock();
            if (entry)
              {
                if (entry->playback_count.load())
                  {
                    playback_entries.push_back (entry);
                    entry->playing = true;
                  }
              }
          }
      }
    for (const auto& entry : playback_entries)
      load (entry.get());
  }
  void
  unload (Entry *entry)
  {
    SampleBufferVector new_buffers;
    new_buffers.resize (entry->buffers.size());
    for (size_t b = 0; b < entry->buffers.size(); b++)
    {
        if (b > 20)
          {
            new_buffers[b].data   = nullptr;
          }
        else
          {
            new_buffers[b].data   = entry->buffers[b].data.load();
          }
      }
    auto free_function = entry->buffers.take_atomically (new_buffers);
    entry->free_functions.push_back (free_function);

  }
  void
  cleanup_unused_data()
  {
    double now = get_time();
    if (fabs (now - last_cleanup_time) < 0.5)
      return;
    last_cleanup_time = now;
    for (const auto& [key, value] : cache)
      {
        auto entry = value.lock();
        if (entry)
          {
            if (!entry->playback_count.load())
              {
                if (entry->playing)
                  {
                    unload (entry.get());
                    // printf ("unloaded %s / %s\n", entry->filename.c_str(), cache_stats().c_str());

                    entry->max_buffer_index = 0;
                    entry->playing = false;
                  }
              }
            if (!entry->playback_count.load()) // need to repeat this check to be sure no new readers exist
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
    sf_pool.cleanup();
  }
  void
  update_size_bytes (int delta_bytes)
  {
    atomic_n_total_bytes_ += delta_bytes;
  }
  std::string
  cache_stats()
  {
    return string_printf ("cache holds %.2f MB in %d entries", atomic_n_total_bytes_ / 1024. / 1024., atomic_cache_file_count_.load());
  }
  size_t
  cache_size()
  {
    static_assert (decltype (atomic_n_total_bytes_)::is_always_lock_free);
    return atomic_n_total_bytes_;
  }
  uint
  cache_file_count()
  {
    static_assert (decltype (atomic_cache_file_count_)::is_always_lock_free);
    return atomic_cache_file_count_;
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

          load_data_for_playback_entries();
          cleanup_unused_data();

        }
        usleep (20 * 1000);
      }
  }
};

inline
SampleBuffer::Data::Data (SampleCache *sample_cache, size_t n_samples) :
  sample_cache_ (sample_cache),
  n_samples_ (n_samples),
  samples (n_samples)
{
  sample_cache_->update_size_bytes (n_samples_ * sizeof (decltype (samples)::value_type));
}

inline
SampleBuffer::Data::~Data()
{
  sample_cache_->update_size_bytes (-n_samples_ * sizeof (decltype (samples)::value_type));
}

}

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
