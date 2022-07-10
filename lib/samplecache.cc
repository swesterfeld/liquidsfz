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
using std::string;

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

bool
Sample::preload (const string& filename)
{
  SF_INFO sfinfo = { 0, };
  auto sf = sample_cache->sf_pool().open (filename, &sfinfo);
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

  size_t n_buffers = 0;
  size_t n_preload = preload_buffer_count();
  while (pos < frames)
    {
      n_buffers++;
      pos += SampleBuffer::frames_per_buffer;
    }
  buffers_.resize (n_buffers);
  for (size_t b = 0; b < n_buffers; b++)
    {
      if (b < n_preload)
        load_buffer (sf->sndfile, b);
    }
  return true;
}

uint
Sample::preload_buffer_count()
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

  uint n_preload = 0;

  sf_count_t frames = n_samples_ / channels_;
  sf_count_t pos = 0;
  while (pos < frames)
    {
      double time_ms = pos * 1000. / sample_rate_;
      if (time_ms < preload_time_ms)
        n_preload++;

      pos += SampleBuffer::frames_per_buffer;
    }
  return n_preload;
}

void
Sample::load_buffer (SNDFILE *sndfile, size_t b)
{
  auto& buffer = buffers_[b];
  if (!buffer.data)
    {
      auto data = new SampleBuffer::Data (sample_cache, (SampleBuffer::frames_per_buffer + SampleBuffer::frames_overlap) * channels_);
      data->start_n_values = (b * SampleBuffer::frames_per_buffer - SampleBuffer::frames_overlap) * channels_;

      // FIXME: may want to check return codes
      sf_seek (sndfile, b * SampleBuffer::frames_per_buffer, SEEK_SET);
      sf_readf_float (sndfile, &data->samples[SampleBuffer::frames_overlap * channels_], SampleBuffer::frames_per_buffer);

      if (b > 0)
        {
          // copy last samples from last buffer to first samples from this buffer to make buffers overlap
          const auto last_data = buffers_[b - 1].data.load();
          const float *from_samples = &last_data->samples[SampleBuffer::frames_per_buffer * channels_];
          std::copy_n (from_samples, SampleBuffer::frames_overlap * channels_, &data->samples[0]);
        }

      buffer.data = data;

      last_update = sample_cache->next_update_counter();
    }
}

void
Sample::load()
{
  uint max_b = max_buffer_index_.load() + preload_buffer_count();

  for (size_t b = 0; b < buffers_.size(); b++)
    {
      if (!buffers_[b].data && b < max_b)
        {
          SF_INFO sfinfo;
          auto sf = SFPool::use_mmap ? mmap_sf_ : sample_cache->sf_pool().open (filename_, &sfinfo);

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
  uint n_preload = preload_buffer_count();

  SampleBufferVector new_buffers;
  new_buffers.resize (buffers_.size());
  for (size_t b = 0; b < buffers_.size(); b++)
  {
      if (b < n_preload)
        {
          new_buffers[b].data   = buffers_[b].data.load();
        }
      else
        {
          new_buffers[b].data   = nullptr;
        }
    }
  auto free_function = buffers_.take_atomically (new_buffers);
  free_functions.push_back (free_function);
  unload_possible = false;
}

}
