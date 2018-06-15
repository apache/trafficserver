/**
  @file Extendible

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

  @section details Details

////////////////////////////////////////////
  Implements advanced locking techniques:
  * LockPool
  * Writer_ptr
*/

#pragma once
#include <mutex>
#include <vector>
#include <memory>

//////////////////////////////////////////////////////////
// Lock Pool
/// Intended to make datasets thread safe by assigning locks to stripes of data, kind of like a bloom filter.
/** Allocates a fixed number of locks and retrives one with a hash.
 */
template <typename Mutex_t> struct LockPool {
  /**
   * @param numLocks - use a prime number near the number of concurrent users you expect
   */
  LockPool(size_t num_locks) : mutexes(num_locks) {}

  Mutex_t &
  getMutex(size_t key_hash)
  {
    return mutexes[key_hash % size()];
  }

  size_t
  size() const
  {
    return mutexes.size();
  }

  void
  lockAll()
  {
    for (Mutex_t &m : mutexes) {
      m.lock();
    }
  }

  void
  unlockAll()
  {
    for (Mutex_t &m : mutexes) {
      m.unlock();
    }
  }

private:
  std::vector<Mutex_t> mutexes;

  /// use the other constructor to define how many locks you want.
  LockPool() = delete;
};

template <typename T> class write_ptr;
template <typename T> class read_ptr;

using ReadMutex_t = std::mutex; // TODO: use shared_mutex when available
using ReadLock_t  = std::unique_lock<ReadMutex_t>;
ReadMutex_t &ReadMemMutex(void const *ptr); // used for read, and write swap

using WriteMutex_t = std::mutex;
using WriteLock_t  = std::unique_lock<WriteMutex_t>;
WriteMutex_t &WriteMemMutex(void const *ptr); // used for write block

///////////////////////////////////////////
/// read_ptr
/** just a thread safe shared pointer.
 *
 */

template <typename T> class read_ptr
{
private:
  std::shared_ptr<T> data_ptr;

public:
  read_ptr(const read_ptr &reader) = delete;
  read_ptr &operator=(const read_ptr &other) = delete;

  read_ptr() : data_ptr(new T()) {}
  read_ptr(T *data) : data_ptr(data) {}

  const std::shared_ptr<const T>
  get() const
  { // get shared access to the current pointer
    auto read_lock = ReadLock_t(ReadMemMutex(&data_ptr));
    // copy the pointer
    return data_ptr;
    //[end scope] unlock read_lock
  }

  void
  reset(T *data)
  {
    // get exclusive access to the pointer
    auto write_lock = WriteLock_t(ReadMemMutex(&data_ptr));
    // copy the pointer
    data_ptr.reset(data);
    //[end scope] unlock write_lock
  }
};

///////////////////////////////////////////
/// write_ptr

/// an exclusive write pointer that updates a shared_ptr on destrustion.
/** used for COPY_SWAP functionality.
 * 1. copy data (construct)
 * 2. overwrite data (scope)
 * 3. update live data pointer (destruct)
 */
template <typename T> class write_ptr : public std::unique_ptr<T>
{
private:
  WriteLock_t write_lock; // block other writers from starting
  read_ptr<T> &reader;    // shared read access pointer location

public:
  write_ptr()                  = delete;
  write_ptr(const write_ptr &) = delete;
  write_ptr &operator=(const write_ptr<T> &) = delete;

  write_ptr(read_ptr<T> &data_reader) : write_lock(WriteMemMutex(&data_reader)), reader(data_reader)
  {
    // wait for exclusive write access to the reader
    // copy the data to new memory
    std::unique_ptr<T>::reset(new T(*reader.get()));
  }
  write_ptr(write_ptr &&other) : std::unique_ptr<T>(std::move(other)), write_lock(std::move(other.write_lock)), reader(other.reader)
  {
  }

  ~write_ptr()
  {
    if (!write_lock) {
      return; // previously aborted
    }

    // point the existing read ptr to the newly written data
    reader.reset(std::unique_ptr<T>::release());
  }

  void
  abort()
  {
    write_lock.unlock();         // allow other writers to start
    std::unique_ptr<T>::reset(); // delete data copy
  }
};
