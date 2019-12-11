/** @file

  Classes for proper mutual exclusion for a single writer and multiple
  readers of a data structure.  If writes are infrequent relative to reads,
  these classes allow reading to generally occur without blocking the
  thread.  The lock() and unlock() member functions below all provide a
  strong memory fence (Sequentially-consistent ordering).

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
#include <mutex>
#include <condition_variable>

namespace ts
{
class OneWriterMultiReader
{
protected:
  struct BasicWriteLock // Meets BasicLockable Standard Library requirements.
  {
    BasicWriteLock(OneWriterMultiReader &owmr_) : owmr(owmr_) {}

    void lock();

    void unlock();

    OneWriterMultiReader &owmr;

    // Disallow copying because lock ownership is unique.  Disallowing moving since I can't think of a case when
    // it would be a good idea.
    //
    BasicWriteLock(const BasicWriteLock &) = delete;
    BasicWriteLock &operator=(const BasicWriteLock &) = delete;
  };

public:
  class ReadLock // Meets Lockable Standard Library requirements.
  {
  public:
    ReadLock(OneWriterMultiReader &owmr) : _owmr{owmr} { lock(); }

    ReadLock(OneWriterMultiReader &owmr, std::defer_lock_t) : _owmr{owmr} {}

    bool try_lock();

    void lock();

    void unlock();

    bool
    is_locked() const
    {
      return locked;
    }

    ~ReadLock() { unlock(); }

    // Disallow copying because lock ownership is unique.  Disallowing moving since I can't think of a case when
    // it would be a good idea.
    //
    ReadLock(const ReadLock &) = delete;
    ReadLock &operator=(const ReadLock &) = delete;

  private:
    OneWriterMultiReader &_owmr;

    bool locked{false};
  };

  // User code must ensure that, while one thread has a write lock on a OneWriterMultiReader instance, no other
  // thread attempts to get a write lock on the same instance.
  //
  class WriteLock : private BasicWriteLock // Meets BasicLockable Standard Library requirements.
  {
  public:
    WriteLock(OneWriterMultiReader &owmr) : BasicWriteLock{owmr} { lock(); }

    WriteLock(OneWriterMultiReader &owmr, std::defer_lock_t) : BasicWriteLock{owmr} {}

    void
    lock()
    {
      BasicWriteLock::lock();

      locked = true;
    }

    void
    unlock()
    {
      if (locked) {
        BasicWriteLock::unlock();

        locked = false;
      }
    }

    bool
    is_locked() const
    {
      return locked;
    }

    ~WriteLock() { unlock(); }

  private:
    bool locked{false};
  };

private:
  // The most significant bit of _status is a write pending flag, set by a writer to indicate a pending write,
  // and cleared when the write is completed.  The other bits hold a count of active readers.
  //
  std::atomic<unsigned> _status{0};

  static const unsigned Reader_count_mask  = (~static_cast<unsigned>(0)) >> 1;
  static const unsigned Write_pending_mask = ~Reader_count_mask;

  // This mutex is to allow a writer to check that the reader count is non-zero and block on the clear
  // condition variable as an atomic operation.
  //
  std::mutex _clear_reader_count;

  std::condition_variable _reader_count_cleared;

  // This mutex is to allow a readaer to check that the write pending flag is set and block on the clear
  // condition variable as an atomic operation.
  //
  std::mutex _clear_write_pending;

  std::condition_variable _write_pending_cleared;
};

class ExclusiveWriterMultiReader : private OneWriterMultiReader
{
public:
  class ReadLock : public OneWriterMultiReader::ReadLock // Meets Lockable Standard Library requirements.
  {
  public:
    ReadLock(ExclusiveWriterMultiReader &owmr) : OneWriterMultiReader::ReadLock(owmr) {}

    ReadLock(ExclusiveWriterMultiReader &owmr, std::defer_lock_t) : OneWriterMultiReader::ReadLock(owmr, std::defer_lock) {}
  };

  // If one thread has a write lock on as ExclusiveWriterMultiReader instance, and another thread attempts to get a
  // a write lock on the same instance, that thread will block until the first write lock is released.
  //
  class WriteLock // Meets BasicLockable Standard Library requirements.
  {
  public:
    WriteLock(ExclusiveWriterMultiReader &owmr) : _wl{owmr} { lock(); }

    WriteLock(ExclusiveWriterMultiReader &owmr, std::defer_lock_t) : _wl{owmr} {}

    void
    lock()
    {
      _write().lock();

      _wl.lock();

      locked = true;
    }

    void
    unlock()
    {
      if (locked) {
        locked = false;

        _wl.unlock();

        _write().unlock();
      }
    }

    bool
    is_locked() const
    {
      return locked;
    }

    ~WriteLock() { unlock(); }

  private:
    OneWriterMultiReader::BasicWriteLock _wl;

    std::mutex &
    _write()
    {
      return static_cast<ExclusiveWriterMultiReader &>(_wl.owmr)._write;
    }

    bool locked{false};
  };

private:
  std::mutex _write;
};

} // end namespace ts
