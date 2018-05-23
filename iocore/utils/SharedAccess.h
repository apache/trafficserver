/**
  @file SharedExtendible

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
/** Allocates a fixed number of locks and retrives one with a hash. */
template <typename Mutex_t> struct LockPool {
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

  /// please use the other constructor to define how many locks you want.
  LockPool(){};
};

///////////////////////////////////////////
// writer_ptr

using WriterMutex_t = std::mutex; // TODO: maybe use shared_mutex for read_access
using WriterLock_t  = std::unique_lock<WriterMutex_t>;

WriterMutex_t &SharedAccessMutex(void const *ptr);

WriterMutex_t &SharedWriterMutex(void const *ptr);

/// an exclusive write pointer that updates a shared_ptr on destrustion.
/** used for COPY_SWAP functionality.
 * 1. copy data (construct)
 * 2. overwrite data (scope)
 * 3. update live data pointer (destruct)
 */
template <typename T> class writer_ptr : public std::unique_ptr<T>
{
private:
  WriterLock_t write_lock;          // block other writers from starting
  std::shared_ptr<T> &read_ptr_loc; // shared read access pointer location

public:
  writer_ptr() {}
  writer_ptr(std::shared_ptr<T> &data_ptr) : read_ptr_loc(data_ptr)
  {
    // get write access to the memory address
    write_lock = WriterLock_t(SharedWriterMutex(&data_ptr));
    // copy the data to new memory
    std::unique_ptr<T>::reset(new T(*data_ptr));
  }

  writer_ptr(writer_ptr &&other) : write_lock(std::move(other.write_lock)), read_ptr_loc(other.read_ptr_loc)
  {
    std::unique_ptr<T>::reset(other.std::unique_ptr<T>::release());
  }

  void
  abort()
  {
    write_lock.release();
    delete this;
  }

  ~writer_ptr()
  {
    if (!write_lock) {
      return;
    }

    // get an exclusive lock for the read access pointer
    WriterLock_t access_lock(SharedAccessMutex(&read_ptr_loc));

    // point the existing read ptr to the newly written data
    read_ptr_loc = std::shared_ptr<T>(this->release());

    //[end scope] unlock access_lock and write_lock
  }
};