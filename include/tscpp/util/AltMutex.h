/** @file

   An alternative to std::mutex for low contention mutual exclusion.  Uses one unsigned int as data.
   (sizof(std::mutex) == 40 for gcc on x86.) Locking/unlocking will be very fast when there is no
   contention.  When there is contention, the latency to get the lock after it is unlocked by another
   thread wil likely be longer (on the order of 10ms).  If the wait for the lock exceeds a timer tick,
   there will likely be unnecessary context switching between threads.

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#pragma once

#include <atomic>
#include <thread>
#include <chrono>

#include <tscore/ink_assert.h>

namespace ts
{
class AltMutex
{
public:
  bool try_lock() noexcept;

  // WARNING:  Not recursive.
  //
  void lock();

  void unlock() noexcept;

  // Notes:  all 3 member functions act as a strong memory fence.  The atomic data member suppresses
  // copying/moving member functions.

private:
  // This is incremented on locking and unlocking, so it is odd when the lock is locked.
  //
  std::atomic<unsigned> lock_count{0};
};

inline bool
AltMutex::try_lock() noexcept
{
  unsigned lc = lock_count;

  if (lc & 1) {
    // Already locked.
    //
    return false;
  }

  return lock_count.compare_exchange_strong(lc, lc + 1);
}

inline void
AltMutex::lock()
{
  unsigned tries = 0;

  for (;;) {
    if (try_lock()) {
      break;
    }

    ++tries;

    if (tries < 20) {
      std::this_thread::yield();

    } else {
      // Failsafe check for deadlock.
      //
      ink_release_assert(tries < 50000);

      // This may be a case of priority inversion, sleep to ensure the thread holding the lock can run.
      //
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

inline void
AltMutex::unlock() noexcept
{
  if (lock_count & 1) {
    ++lock_count;

  } else {
    ink_assert(false);
  }
}

} // end namespace ts
