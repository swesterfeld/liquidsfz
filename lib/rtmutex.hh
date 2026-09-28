// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#pragma once

#include <atomic>

namespace LiquidSFZInternal
{

/*
 * std::mutex try_lock() / unlock() is not hard RT safe, so this is
 * an alternative mutex implementation where these two methods are
 */
class RTMutex
{
  std::atomic_flag locked_flag = ATOMIC_FLAG_INIT;
public:
  bool try_lock();
  void wait_for_lock();
  void unlock();
};

class RTMutexWaitLockGuard
{
  RTMutex& mutex_;
public:
  explicit RTMutexWaitLockGuard (RTMutex& m)
    : mutex_ (m)
  {
    mutex_.wait_for_lock();
  }
  ~RTMutexWaitLockGuard()
  {
    mutex_.unlock();
  }

  // non-copyable
  RTMutexWaitLockGuard (const RTMutexWaitLockGuard&) = delete;
  RTMutexWaitLockGuard& operator= (const RTMutexWaitLockGuard&) = delete;

  // non-movable (same as std::lock_guard)
  RTMutexWaitLockGuard (RTMutexWaitLockGuard&&) = delete;
  RTMutexWaitLockGuard& operator= (RTMutexWaitLockGuard&&) = delete;
};

}
