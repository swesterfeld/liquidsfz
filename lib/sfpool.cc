// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "sfpool.hh"
#include "utils.hh"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <algorithm>

using std::string;
using std::vector;

namespace LiquidSFZInternal
{

sf_count_t
SFPool::Entry::seek_read_frames (sf_count_t pos, float *buffer, sf_count_t frame_count)
{
  // FIXME: may want to check return codes
  if (position_ != pos)
    {
      sf_seek (sndfile, pos, SEEK_SET);
      position_ = pos;
    }
  sf_count_t n_frames = sf_readf_float (sndfile, buffer, frame_count);
  if (n_frames > 0)
    position_ += n_frames;
  return n_frames;
}

SFPool::Entry::~Entry()
{
  if (sndfile)
    {
      sf_close (sndfile);
      sndfile = nullptr;

      if (mapped_data.mem)
        {
          munmap (mapped_data.mem, mapped_data.size);
          mapped_data.mem = nullptr;
        }
    }
}

SNDFILE *
SFPool::mmap_open (const string& filename, SF_INFO *sfinfo, SFPool::EntryP entry)
{
  int fd = ::open (filename.c_str(), O_RDONLY);
  if (fd == -1)
    return nullptr; // FIXME: handle error

  struct stat sb;
  if (fstat (fd, &sb) == -1)
    return nullptr; // FIXME: handle error

  unsigned char *addr = (unsigned char *) mmap (nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED)
    return nullptr; // FIXME: Handle error

  close (fd);

  entry->mapped_data.mem  = addr;
  entry->mapped_data.size = sb.st_size;

  entry->mapped_data.io.get_filelen = [] (void *data) {
    MappedVirtualData *vdata = static_cast<MappedVirtualData *> (data);
    return vdata->size;
  };

  entry->mapped_data.io.seek = [] (sf_count_t offset, int whence, void *data) {
    MappedVirtualData *vdata = static_cast<MappedVirtualData *> (data);
    if (whence == SEEK_CUR)
      {
        vdata->offset = vdata->offset + offset;
      }
    else if (whence == SEEK_SET)
      {
        vdata->offset = offset;
      }
    else if (whence == SEEK_END)
      {
        vdata->offset = vdata->size + offset;
      }

    /* can't seek beyond eof */
    vdata->offset = std::clamp<sf_count_t> (vdata->offset, 0, vdata->size);
    return vdata->offset;
  };

  entry->mapped_data.io.read = [] (void *ptr, sf_count_t count, void *data) {
    MappedVirtualData *vdata = static_cast<MappedVirtualData *> (data);

    sf_count_t rcount = 0;
    if (vdata->offset + count <= vdata->size)
      {
        /* fast case: read can be fully satisfied with the data we have */
        memcpy (ptr, vdata->mem + vdata->offset, count);
        rcount = count;
      }
    else
      {
        unsigned char *uptr = static_cast<unsigned char *> (ptr);
        for (sf_count_t i = 0; i < count; i++)
          {
            sf_count_t rpos = i + vdata->offset;
            if (rpos < vdata->size)
              {
                uptr[i] = vdata->mem[rpos];
                rcount++;
              }
          }
      }
    vdata->offset += rcount;
    return rcount;
  };

  entry->mapped_data.io.tell = [] (void *data) {
    MappedVirtualData *vdata = static_cast<MappedVirtualData *> (data);
    return vdata->offset;
  };

  entry->mapped_data.io.write = nullptr;

  return sf_open_virtual (&entry->mapped_data.io, SFM_READ, sfinfo, &entry->mapped_data);
}

SFPool::EntryP
SFPool::open (const string& filename, SF_INFO *sfinfo)
{
  EntryP entry = cache[filename];
  if (entry)
    {
      entry->time = get_time();
      *sfinfo = entry->sfinfo;
      return entry;
    }

  entry = std::make_shared<Entry>();
  entry->filename = filename;
  if (use_mmap)
    entry->sndfile = mmap_open (filename, sfinfo, entry);
  else
    entry->sndfile = sf_open (filename.c_str(), SFM_READ, sfinfo);
  entry->sfinfo = *sfinfo;
  entry->time = get_time();
  cache[filename] = entry;
  // printf ("sf_open %s -> %p\n", filename.c_str(), entry->sndfile);

  cleanup(); // close old files if any
  return entry;
}

void
SFPool::cleanup()
{
  if (use_mmap)
    {
      /* for mmap, the sample cache will keep all files open that it needs */
      auto it = cache.begin();
      while (it != cache.end())
        {
          if (it->second.use_count() == 1) // are we the only user?
            {
              it = cache.erase (it);
            }
          else
            {
              it++;
            }
        }
    }
  else
    {
      auto close_candidates = [&]() {
        vector<EntryP> close_candidates;

        for (auto [ filename, entry ] : cache)
          close_candidates.push_back (entry);
        std::sort (close_candidates.begin(), close_candidates.end(), [] (auto& a, auto& b) { return a->time < b->time; });

        return close_candidates;
      };

      /* enforce time limit */
      double now = get_time();
      for (auto cc : close_candidates())
        {
          if (fabs (cc->time - now) > max_time)
            cache.erase (cc->filename);
        }


      /* enforce max_fds limit */
      while (cache.size() > max_fds)
        {
          auto cc = close_candidates()[0];
          cache.erase (cc->filename);
        }
    }
}

}
