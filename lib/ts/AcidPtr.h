/**
  @file AcidPtr

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
  LockPool()                 = delete;
  LockPool(LockPool const &) = delete;
};

template <typename T> class AcidCommitPtr;
template <typename T> class AcidPtr;

using AcidPtrMutex = std::mutex; // TODO: use shared_mutex when available
using AcidPtrLock  = std::unique_lock<AcidPtrMutex>;
AcidPtrMutex &AcidPtrMutexGet(void const *ptr); // used for read, and write swap

using AcidCommitMutex = std::mutex;
using AcidCommitLock  = std::unique_lock<AcidCommitMutex>;
AcidCommitMutex &AcidCommitMutexGet(void const *ptr); // used for write block

///////////////////////////////////////////
/// AcidPtr
/** just a thread safe shared pointer.
 *
 */

template <typename T> class AcidPtr
{
private:
  std::shared_ptr<T> data_ptr;

public:
  AcidPtr(const AcidPtr &) = delete;
  AcidPtr &operator=(const AcidPtr &) = delete;

  AcidPtr() : data_ptr(new T()) {}
  AcidPtr(T *data) : data_ptr(data) {}

  const std::shared_ptr<const T>
  getPtr() const
  { // wait until we have exclusive pointer access.
    auto ptr_lock = AcidPtrLock(AcidPtrMutexGet(&data_ptr));
    // copy the pointer
    return data_ptr;
    //[end scope] unlock ptr_lock
  }

  void
  commit(T *data)
  {
    // wait until existing commits finish, avoid race conditions
    auto commit_lock = AcidCommitLock(AcidCommitMutexGet(&data_ptr));
    // wait until we have exclusive pointer access.
    auto ptr_lock = AcidPtrLock(AcidPtrMutexGet(&data_ptr));
    // overwrite the pointer
    data_ptr.reset(data);
    //[end scope] unlock commit_lock & ptr_lock
  }

  AcidCommitPtr<T>
  startCommit()
  {
    return AcidCommitPtr<T>(*this);
  }

  friend class AcidCommitPtr<T>;

protected:
  void
  _finishCommit(T *data)
  {
    // wait until we have exclusive pointer access.
    auto ptr_lock = AcidPtrLock(AcidPtrMutexGet(&data_ptr));
    // overwrite the pointer
    data_ptr.reset(data);
    //[end scope] unlock ptr_lock
  }
};

///////////////////////////////////////////
/// AcidCommitPtr

/// a globally exclusive pointer, for commiting changes to AcidPtr.
/** used for COPY_SWAP functionality.
 * 1. copy data (construct)
 * 2. overwrite data (scope)
 * 3. update live data pointer (destruct)
 */
template <typename T> class AcidCommitPtr : public std::unique_ptr<T>
{
private:
  AcidCommitLock commit_lock; // block other writers from starting
  AcidPtr<T> &data;           // data location

public:
  AcidCommitPtr()                      = delete;
  AcidCommitPtr(const AcidCommitPtr &) = delete;
  AcidCommitPtr &operator=(const AcidCommitPtr<T> &) = delete;

  AcidCommitPtr(AcidPtr<T> &data_ptr) : commit_lock(AcidCommitMutexGet(&data_ptr)), data(data_ptr)
  {
    // wait for exclusive commit access to the data
    // copy the data to new memory
    std::unique_ptr<T>::reset(new T(*data.getPtr()));
  }
  AcidCommitPtr(AcidCommitPtr &&other)
    : std::unique_ptr<T>(std::move(other)), commit_lock(std::move(other.commit_lock)), data(other.data)
  {
  }

  ~AcidCommitPtr()
  {
    if (!commit_lock) {
      return; // previously aborted
    }

    // point the existing read ptr to the newly written data
    data._finishCommit(std::unique_ptr<T>::release());
  }

  void
  abort()
  {
    commit_lock.unlock();        // allow other writers to start
    std::unique_ptr<T>::reset(); // delete data copy
  }
};
