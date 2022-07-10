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

namespace LiquidSFZInternal {

// this is slow anyway, and we do not want this to be inlined
bool
Sample::PlayHandle::lookup (sample_count_t pos)
{
  int buffer_index = (pos + SampleBuffer::frames_overlap * sample_->channels) / (SampleBuffer::frames_per_buffer * sample_->channels);
  if (buffer_index >= 0 && buffer_index < int (sample_->buffers.size()))
    {
      sample_->update_max_buffer_index (buffer_index);

      const SampleBuffer::Data *data = sample_->buffers[buffer_index].data.load();
      if (!live_mode_)
        {
          /* when not in live mode, we need to block until the background
           * thread has completed loading the block
           */
          while (!data)
            {
              usleep (20 * 1000); // FIXME: may want to synchronize without sleeping
              data = sample_->buffers[buffer_index].data.load();
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

}
