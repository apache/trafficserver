/** @file

  Implementation file for OneWriterMultiReader.h.

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

#include <tscore/ink_assert.h>

#include "OneWriterMultiReader.h"

namespace ts
{
bool
OneWriterMultiReader::ReadLock::try_lock()
{
  ink_assert(!locked);

  unsigned s = _owmr._status;

  if (s & Write_pending_mask) {
    return false;
  }
  locked = _owmr._status.compare_exchange_weak(s, s + 1);
  return locked;
}

void
OneWriterMultiReader::ReadLock::lock()
{
  ink_assert(!locked);

  unsigned s = _owmr._status;

  // Retry loop.
  //
  for (;;) {
    if (s & Write_pending_mask) {
      {
        std::unique_lock<std::mutex> ul(_owmr._clear_write_pending);

        // Make sure still set now that we have the lock.
        //
        s = _owmr._status;
        if (s & Write_pending_mask) {
          _owmr._write_pending_cleared.wait(ul);
        }
      }
      s = _owmr._status;

    } else if (_owmr._status.compare_exchange_weak(s, s + 1)) {
      break;
    }
  }
  locked = true;
}

void
OneWriterMultiReader::ReadLock::unlock()
{
  if (!locked) {
    return;
  }

  unsigned s = _owmr._status;

  // Retry loop.
  //
  for (;;) {
    ink_assert((s & Reader_count_mask) > 0);

    if ((Write_pending_mask | 1) == s) {
      _owmr._clear_reader_count.lock();

      // Recheck now that we have the mutex.
      //
      s = _owmr._status;
      if ((Write_pending_mask | 1) == s) {
        if (_owmr._status.compare_exchange_weak(s, Write_pending_mask)) {
          _owmr._clear_reader_count.unlock();
          _owmr._reader_count_cleared.notify_all();
          break;
        }
      }
      _owmr._clear_reader_count.unlock();

    } else if (_owmr._status.compare_exchange_weak(s, s - 1)) {
      break;
    }
  }
  locked = false;
}

void
OneWriterMultiReader::BasicWriteLock::lock()
{
  unsigned s = owmr._status;

  ink_assert(!(s & Write_pending_mask));

  // Retry loop to set write pending flag.
  //
  while (!owmr._status.compare_exchange_weak(s, s | Write_pending_mask)) {
    ink_assert(!(s & Write_pending_mask));
  }

  // Retry loop waiting for readers to finish.
  //
  while (s & Reader_count_mask) {
    {
      std::unique_lock<std::mutex> ul(owmr._clear_reader_count);

      // Recheck now that we have lock.
      //
      s = owmr._status;
      if ((s & Reader_count_mask) == 0) {
        break;
      }
      owmr._reader_count_cleared.wait(ul);
    }
    s = owmr._status;
  }
}

void
OneWriterMultiReader::BasicWriteLock::unlock()
{
  unsigned s = owmr._status;

  ink_assert((s & Write_pending_mask) != 0);

  {
    std::lock_guard<std::mutex> lg(owmr._clear_write_pending);

    // Retry loop.
    //
    while (!owmr._status.compare_exchange_weak(s, s & Reader_count_mask)) {
      ;
    }
  }
  owmr._write_pending_cleared.notify_all();
}

} // end namespace ts

/*

STATE TRANSITIONS for OneWriterMultReader
=========================================

State Tuple (Active Readers, Active Writer, Blocked Readers, Blocked Writer)

0 means none
1 means one
>0 means one or more

Transition Types
----------------
-> Transition that only involves atomic compare-exchange of an unsigned value.  (If atomic operation fails,
   there is no state transition.)
=> Transition where the causing thread must lock a mutex and set or block on a condition variable.

Transition                              Event
----------                              -----
(0, 0, 0, 0)   -> (>0, 0, 0, 0)         Reader attempts lock
(0, 0, 0, 0)   -> (0, 1, 0, 0)          Writer attempts lock
(0, 0, 0, 1)   -> (0, 1, 0, 0)          None (automatic transition)
(0, 0, >0, 1)  -> (0, 1, >0, 0)         None (automatic transition)
(0, 1, 0, 0)   => (0, 1, >0, 0)         Reader attempts lock
(0, 1, 0, 0)   => (0, 0, 0, 0)          Writer unlocks
(0, 1, >0, 0)  => (0, 1, >0, 0)         Reader attempts lock
(0, 1, >0, 0)  => (0, 0, 0, 0)          Writer unlocks
(>0, 0, 0, 0)  -> (>0, 0, 0, 0)         Reader attempts lock
(>0, 0, 0, 0)  -> (>0, 0, 0, 0)         Reader unlocks (multiple current readers)
(>0, 0, 0, 0)  -> (0, 0, 0, 0)          Reader unlocks (one current reader)
(>0, 0, 0, 0)  => (>0, 0, 0, 1)         Writer attempts lock
(>0, 0, 0, 1)  => (>0, 0, >0, 1)        Reader attempts lock
(>0, 0, 0, 1)  -> (>0, 0, 0, 1)         Reader unlocks (multiple current readers)
(>0, 0, 0, 1)  => (0, 0, 0, 1)          Reader unlocks (one current reader)
(>0, 0, >0, 1) => (>0, 0, >0, 1)        Reader attempts lock
(>0, 0, >0, 1) -> (>0, 0, >0, 1)        Reader unlocks (multiple current readers)
(>0, 0, >0, 1) => (0, 0, >0, 1)         Reader unlocks (one current reader)

(The reader count in _status is the number of active readers and blocked readers.  The write pending
bit in _status is the logical OR of writer active and writer pending.)

*/
