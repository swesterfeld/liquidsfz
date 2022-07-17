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

#include "samplecache.hh"

using std::max;
using std::min;
using std::string;
using std::vector;

using namespace std::chrono_literals;

namespace LiquidSFZInternal {

// this is slow anyway, and we do not want this to be inlined
bool
Sample::PlayHandle::lookup (sample_count_t pos)
{
  int buffer_index = (pos + SampleBuffer::frames_overlap * sample_->channels_) / (SampleBuffer::frames_per_buffer * sample_->channels_);
  if (buffer_index >= 0 && buffer_index < int (sample_->buffers_.size()))
    {
      sample_->update_max_buffer_index (buffer_index);

      const SampleBuffer::Data *data = sample_->buffers_[buffer_index].data.load();
      if (!live_mode_ && !data)
        {
          /* when not in live mode, we need to wakeup the background thread and
           * block until it has completed loading the block
           */
          sample_->sample_cache_->trigger_load_and_wait();

          data = sample_->buffers_[buffer_index].data.load();
        }
      if (data)
        {
          assert (pos >= data->start_n_values);

          samples_   = data->samples();
          start_pos_ = data->start_n_values;
          end_pos_   = start_pos_ + data->n_samples();

          return true;
        }
    }
  samples_ = nullptr;
  start_pos_ = end_pos_ = 0;
  return false;
}

Sample::Sample (SampleCache *sample_cache)
  : sample_cache_ (sample_cache)
{
}

Sample::~Sample()
{
  if (playing())
    {
      fprintf (stderr, "liquidsfz: error Sample is deleted while playing (this should not happen)\n");
    }
  else
    {
      free_unused_data();
      buffers_.clear();
    }
}

Sample::PreloadInfoP
Sample::add_preload (uint time_ms, uint offset)
{
  auto preload_info = std::make_shared<PreloadInfo>();

  preload_info->time_ms = time_ms;
  preload_info->offset = offset;

  preload_infos.push_back (preload_info);
  return preload_info;
}

bool
Sample::preload (const string& filename)
{
  SF_INFO sfinfo = { 0, };
  auto sf = sample_cache_->sf_pool().open (filename, &sfinfo);
  SNDFILE *sndfile = sf->sndfile;

  if (!sndfile)
    return false;

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

  /* load loop points */
  SF_INSTRUMENT instrument = {0,};
  if (sf_command (sndfile, SFC_GET_INSTRUMENT, &instrument, sizeof (instrument)) == SF_TRUE)
    {
      if (instrument.loop_count)
        {
          if (instrument.loops[0].mode == SF_LOOP_FORWARD)
            {
              loop_ = true;
              loop_start_ = instrument.loops[0].start;
              loop_end_ = instrument.loops[0].end;
            }
        }
      }

  sample_rate_ = sfinfo.samplerate;
  channels_ = sfinfo.channels;
  n_samples_ = sfinfo.frames * sfinfo.channels;
  filename_ = filename;

  /* if we use mmap, we keep the file open */
  if (SFPool::use_mmap)
    mmap_sf_ = sf;

  /* preload sample data */
  sf_count_t pos = 0;
  sf_count_t frames = n_samples_ / channels_;

  update_preload_and_read_ahead();

  size_t n_buffers = 0;
  while (pos < frames)
    {
      n_buffers++;
      pos += SampleBuffer::frames_per_buffer;
    }
  buffers_.resize (n_buffers);
  for (size_t b = 0; b < n_buffers; b++)
    {
      if (b < n_preload_buffers_)
        load_buffer (sf.get(), b);
    }
  return true;
}

void
Sample::update_preload_and_read_ahead()
{
  sf_count_t frames = n_samples_ / channels_;

  auto offset_to_ms = [&] (sample_count_t offset) -> uint
    {
      return std::clamp<sample_count_t> (offset, 0, frames) * 1000. / sample_rate_;
    };
  uint preload_time_ms = 0;
  uint read_ahead_time_ms = 0;
  bool cleanup = false;
  for (auto info_weak : preload_infos)
    {
      auto info = info_weak.lock();
      if (info)
        {
          preload_time_ms = max (preload_time_ms, info->time_ms + offset_to_ms (info->offset));
          read_ahead_time_ms = max (read_ahead_time_ms, info->time_ms);
        }
      else
        {
          cleanup = true;
        }
    }

  double buffer_size_ms = 1000.0 * SampleBuffer::frames_per_buffer / sample_rate_;
  n_preload_buffers_ = std::max<size_t> (preload_time_ms / buffer_size_ms + 1, 1);
  n_read_ahead_buffers_ = std::max<size_t> (read_ahead_time_ms / buffer_size_ms + 1, 1);

  if (cleanup)
    {
      // removes old (no longer active) entries
      auto is_old = [] (const auto& info_weak)
        {
          return !info_weak.lock();
        };
      preload_infos.erase (std::remove_if (preload_infos.begin(),
                                           preload_infos.end(),
                                           is_old),
                           preload_infos.end());
    }
}

void
Sample::load_buffer (SFPool::Entry *sf, size_t b)
{
  auto& buffer = buffers_[b];
  if (!buffer.data)
    {
      auto data = new SampleBuffer::Data (sample_cache_, (SampleBuffer::frames_per_buffer + SampleBuffer::frames_overlap) * channels_);
      data->start_n_values = (b * SampleBuffer::frames_per_buffer - SampleBuffer::frames_overlap) * channels_;

      float     *sample_ptr = data->samples() + SampleBuffer::frames_overlap * channels_;

      sf_count_t frames_read = sf->seek_read_frames (b * SampleBuffer::frames_per_buffer, sample_ptr, SampleBuffer::frames_per_buffer);
      if (frames_read != SampleBuffer::frames_per_buffer)
        {
          if (frames_read < 0)
            frames_read = 0;

          float *zero_fill_begin = sample_ptr + frames_read * channels_;
          float *zero_fill_end   = sample_ptr + SampleBuffer::frames_per_buffer * channels_;

          zero_float_block (zero_fill_end - zero_fill_begin, zero_fill_begin);
        }

      if (b > 0)
        {
          // copy last samples from last buffer to first samples from this buffer to make buffers overlap
          const auto last_data = buffers_[b - 1].data.load();
          const float *from_samples = last_data->samples() + SampleBuffer::frames_per_buffer * channels_;
          std::copy_n (from_samples, SampleBuffer::frames_overlap * channels_, data->samples());
        }
      else
        {
          // first buffer: zero samples at start
          zero_float_block (SampleBuffer::frames_overlap * channels_, data->samples());
        }

      buffer.data = data;

      last_update_ = sample_cache_->next_update_counter();
    }
}

void
Sample::load()
{
  update_preload_and_read_ahead();

  size_t load_end = min (max_buffer_index_.load() + n_read_ahead_buffers_, buffers_.size());

  while (load_index_ < load_end)
    {
      if (!buffers_[load_index_].data)
        {
          SF_INFO sfinfo;
          auto sf = SFPool::use_mmap ? mmap_sf_ : sample_cache_->sf_pool().open (filename_, &sfinfo);

          if (sf->sndfile)
            {
              //printf ("loading %s / buffer %zd\n", filename_.c_str(), b);
              load_buffer (sf.get(), load_index_);
              unload_possible_ = true;
            }
        }
      load_index_++;
    }
}

void
Sample::unload()
{
  update_preload_and_read_ahead();

  SampleBufferVector new_buffers;
  new_buffers.resize (buffers_.size());
  for (size_t b = 0; b < buffers_.size(); b++)
  {
      if (b < n_preload_buffers_)
        {
          new_buffers[b].data = buffers_[b].data.load();
        }
      else
        {
          new_buffers[b].data = nullptr;
        }
    }
  auto free_function = buffers_.take_atomically (new_buffers);
  free_functions_.push_back (free_function);

  unload_possible_ = false;
  max_buffer_index_ = 0;
  load_index_ = 0;
}

void
Sample::free_unused_data()
{
  if (!playback_count_.load()) // check to be sure no readers exist
    {
      /*
       * we can safely free the old data structures here because there cannot
       * be any threads that are reading an old version of the sample buffer vector
       * at this point
       */
      for (auto func : free_functions_)
        func();

      free_functions_.clear();
    }
}

SampleCache::SampleCache()
{
  loader_thread_ = std::thread (&SampleCache::background_loader, this);
}

SampleCache::~SampleCache()
{
  {
    std::lock_guard lg (mutex_);
    quit_background_loader_ = true;
    background_loader_cond_.notify_one();
  }
  loader_thread_.join();

  /* free remaining shared ptr references to samples */
  playback_samples_.clear();
  remove_expired_entries();

  if (cache_size() != 0 || cache_file_count() != 0)
    fprintf (stderr, "liquidsfz: cache stats in SampleCache destructor: %s\n", cache_stats().c_str());
}

void
SampleCache::background_loader()
{
  for (;;)
    {
      std::unique_lock lk (mutex_);

      load_data_for_playback_samples();
      cleanup_unused_data();

      if (need_load_done_notify_)
        {
          need_load_done_notify_ = false;
          load_done_cond_.notify_all();
        }
      background_loader_cond_.wait_for (lk, 20ms);

      if (quit_background_loader_)
        return;
    }
}
void
SampleCache::trigger_load_and_wait()
{
  std::unique_lock lk (mutex_);

  need_load_done_notify_ = true;

  background_loader_cond_.notify_one();

  while (need_load_done_notify_)
    load_done_cond_.wait (lk);
}

SampleCache::LoadResult
SampleCache::load (const string& filename, uint preload_time_ms, uint offset)
{
  std::lock_guard lg (mutex_);

  LoadResult result;
  for (const auto& weak : cache_)
    {
      SampleP cached_sample = weak.lock();
      if (cached_sample && cached_sample->filename() == filename) /* already in cache? */
        {
          result.sample = cached_sample;
          result.preload_info = cached_sample->add_preload (preload_time_ms, offset);

          return result;
        }
    }

  auto sample = std::make_shared<Sample> (this);
  auto preload_info = sample->add_preload (preload_time_ms, offset);

  if (sample->preload (filename))
    {
      result.sample = sample;
      result.preload_info = preload_info;

      cache_.push_back (sample);
      atomic_cache_file_count_ = cache_.size();
    }
  return result;
}

void
SampleCache::cleanup_post_load()
{
  std::lock_guard lg (mutex_);

  remove_expired_entries();
}

void
SampleCache::remove_expired_entries()
{
  /* the deletion of the actual cache entry is done automatically (shared ptr)
   * this just removes entries in the std::vector for samples with a null weak ptr
   */
  auto is_old = [] (const auto& weak) { return !weak.lock(); };

  cache_.erase (std::remove_if (cache_.begin(), cache_.end(), is_old),
                cache_.end());

  atomic_cache_file_count_ = cache_.size();
}

void
SampleCache::cleanup_unused_data()
{
  double now = get_time();
  if (fabs (now - last_cleanup_time_) < 0.5)
    return;

  last_cleanup_time_ = now;
  for (const auto& weak : cache_)
    {
      auto sample = weak.lock();
      if (sample)
        sample->free_unused_data();
    }
  sf_pool_.cleanup();

  if (atomic_n_total_bytes_ > atomic_max_cache_size_)
    {
      vector<SampleP> samples;

      for (const auto& weak : cache_)
        {
          auto sample = weak.lock();
          if (sample && !sample->playing() && sample->unload_possible())
            samples.push_back (sample);
        }

      std::sort (samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a->last_update() < b->last_update(); });
      for (auto sample : samples)
        {
          sample->unload();
          sample->free_unused_data();

          //printf ("unloaded %s / %s\n", filename_.c_str(), cache_stats().c_str());

          if (atomic_n_total_bytes_ < atomic_max_cache_size_)
            break;
        }
    }
}

void
SampleCache::load_data_for_playback_samples()
{
  if (playback_samples_need_update_.load())
    {
      playback_samples_need_update_.store (false);

      playback_samples_.clear();
      for (const auto& weak : cache_)
        {
          auto sample = weak.lock();

          if (sample && sample->playing())
            playback_samples_.push_back (sample);
        }
    }
  for (const auto& sample : playback_samples_)
    sample->load();
}

}
