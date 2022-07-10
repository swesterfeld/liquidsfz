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

namespace LiquidSFZInternal {

// this is slow anyway, and we do not want this to be inlined
bool
Sample::PlayHandle::lookup (sample_count_t pos)
{
  int buffer_index = (pos + SampleBuffer::frames_overlap * sample_->channels) / (SampleBuffer::frames_per_buffer * sample_->channels);
  if (buffer_index >= 0 && buffer_index < int (sample_->buffers_.size()))
    {
      sample_->update_max_buffer_index (buffer_index);

      const SampleBuffer::Data *data = sample_->buffers_[buffer_index].data.load();
      if (!live_mode_)
        {
          /* when not in live mode, we need to block until the background
           * thread has completed loading the block
           */
          while (!data)
            {
              usleep (20 * 1000); // FIXME: may want to synchronize without sleeping
              data = sample_->buffers_[buffer_index].data.load();
            }
        }
      if (data)
        {
          assert (pos >= data->start_n_values);

          samples_   = &data->samples[0];
          start_pos_ = data->start_n_values;
          end_pos_   = start_pos_ + data->samples.size();

          return true;
        }
    }
  samples_ = nullptr;
  start_pos_ = end_pos_ = 0;
  return false;
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

void
Sample::preload (SFPool::EntryP sf)
{
  uint preload_time_ms = 0;
  for (auto info_weak : preload_infos)
    {
      auto info = info_weak.lock();
      if (info)
        {
          preload_time_ms = max (preload_time_ms, info->time_ms);
        }
    }
  /* preload sample data */
  sf_count_t pos = 0;
  sf_count_t frames = n_samples / channels;

  size_t n_buffers = 0;
  size_t n_preload = 0;
  while (pos < frames)
    {
      double time_ms = pos * 1000. / sample_rate;
      if (time_ms < preload_time_ms)
        n_preload++;
      n_buffers++;
      pos += SampleBuffer::frames_per_buffer;
    }
  buffers_.resize (n_buffers);
  for (size_t b = 0; b < n_buffers; b++)
    {
      if (b < n_preload)
        load_buffer (sf->sndfile, b);
    }
}

void
Sample::load_buffer (SNDFILE *sndfile, size_t b)
{
  auto& buffer = buffers_[b];
  if (!buffer.data)
    {
      auto data = new SampleBuffer::Data (sample_cache, (SampleBuffer::frames_per_buffer + SampleBuffer::frames_overlap) * channels);
      data->start_n_values = (b * SampleBuffer::frames_per_buffer - SampleBuffer::frames_overlap) * channels;

      // FIXME: may want to check return codes
      sf_seek (sndfile, b * SampleBuffer::frames_per_buffer, SEEK_SET);
      sf_readf_float (sndfile, &data->samples[SampleBuffer::frames_overlap * channels], SampleBuffer::frames_per_buffer);

      if (b > 0)
        {
          // copy last samples from last buffer to first samples from this buffer to make buffers overlap
          const auto last_data = buffers_[b - 1].data.load();
          const float *from_samples = &last_data->samples[SampleBuffer::frames_per_buffer * channels];
          std::copy_n (from_samples, SampleBuffer::frames_overlap * channels, &data->samples[0]);
        }

      buffer.data = data;

      last_update = sample_cache->next_update_counter();
    }
}

void
Sample::load()
{
  for (size_t b = 0; b < buffers_.size(); b++)
    {
      if (!buffers_[b].data && b < size_t (max_buffer_index.load() + 20)) // FIXME: preload time ms
        {
          SF_INFO sfinfo;
          auto sf = SFPool::use_mmap ? mmap_sf : sample_cache->sf_pool().open (filename, &sfinfo);

          if (sf->sndfile)
            {
              //printf ("loading %s / buffer %zd\n", entry->filename.c_str(), b);
              load_buffer (sf->sndfile, b);
              unload_possible = true;
            }
        }
    }
}

void
Sample::unload()
{
  SampleBufferVector new_buffers;
  new_buffers.resize (buffers_.size());
  for (size_t b = 0; b < buffers_.size(); b++)
  {
      if (b > 20) // FIXME: preload time ms
        {
          new_buffers[b].data   = nullptr;
        }
      else
        {
          new_buffers[b].data   = buffers_[b].data.load();
        }
    }
  auto free_function = buffers_.take_atomically (new_buffers);
  free_functions.push_back (free_function);
  unload_possible = false;
}

}
