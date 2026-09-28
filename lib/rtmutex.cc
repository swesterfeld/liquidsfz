// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "rtmutex.hh"

#include <unistd.h>

namespace LiquidSFZInternal
{

bool
RTMutex::try_lock()
{
  return !locked_flag.test_and_set();
}

void
RTMutex::wait_for_lock()
{
  while (!try_lock())
    {
      // this doesn't happen very often and we are in a non-RT thread, so we
      // can block it for some time
      //  => wait for less than one frame drawing time until trying again
      float fps = 240;
      usleep (1000 * 1000 / fps);
    }
}

void
RTMutex::unlock()
{
  locked_flag.clear();
}

}
