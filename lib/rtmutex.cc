// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "rtmutex.hh"

#include <algorithm>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

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
  auto delay = 20us;
  constexpr auto max_delay = 4166us; // one frame at 240 fps

  while (!try_lock())
    {
      // Only non-RT threads may wait. Start with a short sleep and double
      // the delay after each failed attempt, up to max_delay.
      std::this_thread::sleep_for (delay);
      delay = std::min (delay * 2, max_delay);
    }
}

void
RTMutex::unlock()
{
  locked_flag.clear();
}

}
