// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "rtmutex.hh"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;
using LiquidSFZInternal::RTMutex;
using Clock = std::chrono::steady_clock;

int
main()
{
  RTMutex mutex;
  enum class Phase { IDLE, LOCKED, WAITING };
  std::atomic<Phase> phase { Phase::IDLE };
  const std::chrono::microseconds hold_times[] = { 10us, 100us, 500us, 1000us, 5000us };
  constexpr int repeats = 5;

  std::thread rt_thread ([&]
    {
      for (int repeat = 0; repeat < repeats; repeat++)
        for (auto hold_time : hold_times)
          {
            while (!mutex.try_lock())
              std::this_thread::yield();

            // Start the artificial work only once the other thread is ready.
            phase = Phase::LOCKED;
            while (phase != Phase::WAITING)
              std::this_thread::yield();

            const auto end = Clock::now() + hold_time;
            while (Clock::now() < end)
              {} // simulate RT work without sleeping while holding the mutex

            mutex.unlock();
            while (phase != Phase::IDLE)
              std::this_thread::yield();
          }
    });

  // The main thread acts as the non-RT thread. This is a timing experiment;
  // neither thread uses realtime scheduling, so results depend on system load.
  printf ("attempt  RT hold (us)  wait_for_lock (us)\n");
  int attempt = 0;
  for (int repeat = 0; repeat < repeats; repeat++)
    for (auto hold_time : hold_times)
      {
        while (phase != Phase::LOCKED)
          std::this_thread::yield();

        phase = Phase::WAITING;
        const auto start = Clock::now();
        mutex.wait_for_lock();
        const auto elapsed = Clock::now() - start;
        mutex.unlock();
        phase = Phase::IDLE;

        printf ("%7d  %12lld  %18.1f\n", ++attempt,
                static_cast<long long> (hold_time.count()),
                std::chrono::duration<double, std::micro> (elapsed).count());
      }

  rt_thread.join();
}
