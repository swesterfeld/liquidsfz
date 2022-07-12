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

class Sample {
  SampleBufferVector          buffers_;
  SFPool::EntryP              mmap_sf_;
  SampleCache                *sample_cache_ = nullptr;

  std::atomic<int>            playback_count_ = 0;

  std::string                 filename_;

  bool                        loop_ = false;
  int                         loop_start_ = 0;
  int                         loop_end_ = 0;

  uint                        sample_rate_;
  uint                        channels_;
  size_t                      n_samples_ = 0;

  std::atomic<int>            max_buffer_index_ = 0;
  size_t                      load_index_ = 0;
  size_t                      n_preload_buffers_ = 0;
  size_t                      n_read_ahead_buffers_ = 0;

  int64_t                     last_update_ = 0;
  bool                        unload_possible_ = false;

  std::vector<std::function<void()>> free_functions_;

  void update_preload_and_read_ahead();

  void
  update_max_buffer_index (int value)
  {
    int prev_max_index = max_buffer_index_;
    while (prev_max_index < value && !max_buffer_index_.compare_exchange_weak (prev_max_index, value))
      ;
  }
public:
  Sample (SampleCache *sample_cache);
  ~Sample();

  bool
  playing()
  {
    return playback_count_.load() > 0;
  }
  uint
  channels() const
  {
    return channels_;
  }
  sample_count_t
  n_samples() const
  {
    return n_samples_;
  }
  uint
  sample_rate() const
  {
    return sample_rate_;
  }
  bool
  loop() const
  {
    return loop_;
  }
  sample_count_t
  loop_start()
  {
    return loop_start_;
  }
  sample_count_t
  loop_end()
  {
    return loop_end_;
  }
  int64_t
  last_update() const
  {
    return last_update_;
  }
  bool
  unload_possible() const
  {
    return unload_possible_;
  }
  class PlayHandle
  {
  private:
    Sample          *sample_    = nullptr;
    bool             live_mode_ = false;

    const float     *samples_   = nullptr;
    sample_count_t   start_pos_ = 0;
    sample_count_t   end_pos_   = 0;

  public:
    PlayHandle (const PlayHandle& p)
    {
      start_playback (p.sample_, p.live_mode_);
    }
    PlayHandle&
    operator= (const PlayHandle& p)
    {
      start_playback (p.sample_, p.live_mode_);
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
    start_playback (Sample *sample, bool live_mode)
    {
      if (sample != sample_)
        {
          if (sample_)
            sample_->end_playback();

          sample_ = sample;
          if (sample_)
            sample_->start_playback();

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

  void start_playback();
  void end_playback();
  struct PreloadInfo
  {
    uint time_ms = 0;
    uint offset = 0;
  };
  typedef std::shared_ptr<PreloadInfo> PreloadInfoP;

  PreloadInfoP add_preload (uint time_ms, uint offset);
  bool preload (const std::string& filename);
  void load_buffer (SNDFILE *sndfile, size_t b);
  void load();
  void unload();
  void free_unused_data();
private:
  std::vector<std::weak_ptr<PreloadInfo>> preload_infos;
};
typedef std::shared_ptr<Sample> SampleP;

class SampleCache
{
private:
  std::map<std::string, std::weak_ptr<Sample>> cache_;
  std::mutex mutex_;
  std::thread loader_thread_;
  std::atomic<size_t> atomic_n_total_bytes_ = 0;
  std::atomic<uint>   atomic_cache_file_count_ = 0;
  std::atomic<size_t> atomic_max_cache_size_ = 1024 * 1024 * 512;
  SFPool              sf_pool_;
  double              last_cleanup_time_ = 0;
  std::vector<SampleP> playback_samples_;
  std::atomic<bool>   playback_samples_need_update_ = false;
  int64_t             update_counter_ = 0;

  bool quit_background_loader_ = false;

  void remove_expired_entries();
  void load_data_for_playback_samples();
  void background_loader();
  void cleanup_unused_data();

public:
  SampleCache();
  ~SampleCache();

  struct LoadResult
  {
    SampleP sample;
    Sample::PreloadInfoP preload_info;
  };
  LoadResult load (const std::string& filename, uint preload_time_ms, uint offset);
  void cleanup_post_load();

  void
  playback_samples_need_update()
  {
    playback_samples_need_update_.store (true);
  }
  int64_t
  next_update_counter()
  {
    return ++update_counter_;
  }
  SFPool&
  sf_pool()
  {
    return sf_pool_;
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
  void
  set_max_cache_size (size_t max_cache_size)
  {
    static_assert (decltype (atomic_max_cache_size_)::is_always_lock_free);
    atomic_max_cache_size_ = max_cache_size;
  }
  size_t
  max_cache_size()
  {
    return atomic_max_cache_size_;
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

inline void
Sample::start_playback()
{
  playback_count_++;
  sample_cache_->playback_samples_need_update();
}

inline void
Sample::end_playback()
{
  playback_count_--;
  sample_cache_->playback_samples_need_update();
}

}

#endif /* LIQUIDSFZ_SAMPLECACHE_HH */
