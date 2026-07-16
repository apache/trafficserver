/** @file

  Implementation of BRAVO - Biased Locking for Reader-Writer Locks

  Dave Dice and Alex Kogan. 2019. BRAVO: Biased Locking for Reader-Writer Locks.
  In Proceedings of the 2019 USENIX Annual Technical Conference (ATC). USENIX Association, Renton, WA, 315–328.

  https://www.usenix.org/conference/atc19/presentation/dice

  > Section 3.
  >   BRAVO acts as an accelerator layer, as readers can always fall back to the traditional underlying lock to gain read access.
  >   ...
  >   Write performance and the scalability of read-vs-write and write-vs-write behavior depends solely on the underlying lock.

  This code is C++ version of puzpuzpuz/xsync's RBMutex
  https://github.com/puzpuzpuz/xsync/blob/main/rbmutex.go
  Copyright (c) 2021 Andrey Pechkurov

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

#include "tsutil/DenseThreadId.h"
#include "tsutil/Assert.h"
#include "tsutil/ts_thread_safety.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <shared_mutex>
#include <thread>

namespace ts::bravo
{
using time_point = std::chrono::time_point<std::chrono::system_clock>;

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
constexpr std::size_t hardware_constructive_interference_size = 64;
#endif

/**
   ts::bravo::Token

   Token for readers.
   0 is special value that represents inital/invalid value.
 */
using Token = size_t;

/**
   ts::bravo::shared_lock

   Reader guard for shared_mutex_impl, carrying the BRAVO Token. A rigid scoped
   capability: it acquires the shared lock in its constructor and releases it in
   its destructor, with no copy, move, defer or release. That rigidity is what
   lets the analysis track it -- the deferred and movable forms of
   std::shared_lock let the held state escape a single scope and cannot be
   modelled -- and BRAVO readers need only this scoped form.
 */
template <class Mutex> class TS_SCOPED_CAPABILITY shared_lock
{
public:
  using mutex_type = Mutex;

  explicit shared_lock(Mutex &m) TS_ACQUIRE_SHARED(m) : _mutex(&m) { _mutex->lock_shared(_token); }
  ~shared_lock() TS_RELEASE() { _mutex->unlock_shared(_token); }

  ////
  // Neither copyable nor movable: the held shared lock must not escape this scope.
  //
  shared_lock(shared_lock const &)            = delete;
  shared_lock &operator=(shared_lock const &) = delete;
  shared_lock(shared_lock &&)                 = delete;
  shared_lock &operator=(shared_lock &&)      = delete;

  ////
  // Observers
  //
  mutex_type *
  mutex() const
  {
    return _mutex;
  }

  Token
  token() const
  {
    return _token;
  }

  bool
  owns_lock() const
  {
    return _mutex != nullptr;
  }

private:
  mutex_type *_mutex = nullptr;
  Token       _token = 0;
};

/**
   ts::bravo::shared_mutex

   You can use std::lock_guard for writers but, you can't use std::shared_lock for readers to handle ts::bravo::Token.
   Use ts::bravo::shared_lock for readers.

   Set the SLOT_SIZE larger than DenseThreadId::num_possible_values to go fast-path.
 */
template <typename T = std::shared_mutex, size_t SLOT_SIZE = 256, int SLOWDOWN_GUARD = 7>
class TS_CAPABILITY("shared_mutex") shared_mutex_impl
{
public:
  shared_mutex_impl()  = default;
  ~shared_mutex_impl() = default;

  ////
  // No copying or moving.
  //
  shared_mutex_impl(shared_mutex_impl const &)            = delete;
  shared_mutex_impl &operator=(shared_mutex_impl const &) = delete;

  shared_mutex_impl(shared_mutex_impl &&)            = delete;
  shared_mutex_impl &operator=(shared_mutex_impl &&) = delete;

  ////
  // Exclusive locking
  //
  // These lock/unlock methods are the trusted implementation of this capability:
  // their bodies drive the underlying lock (T, by default std::shared_mutex,
  // which libc++ annotates as a capability) across method boundaries and along
  // the BRAVO fast/slow-path branches -- patterns the analysis cannot follow.
  // Exempt the bodies so only the capability contract on each signature is
  // checked; the data-race checking happens at the call sites.
  //
  void
  lock() TS_ACQUIRE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    _mutex.underlying.lock();
    _revoke();
  }

  bool
  try_lock() TS_TRY_ACQUIRE(true) TS_NO_THREAD_SAFETY_ANALYSIS
  {
    bool r = _mutex.underlying.try_lock();
    if (!r) {
      return false;
    }

    _revoke();

    return true;
  }

  void
  unlock() TS_RELEASE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    _mutex.underlying.unlock();
  }

  ////
  // Shared locking (bodies exempted for the same reason as the exclusive ones above)
  //
  void
  lock_shared(Token &token) TS_ACQUIRE_SHARED() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    debug_assert(SLOT_SIZE >= DenseThreadId::num_possible_values());

    // Clear up front so a slow-path acquisition never leaves a stale fast-path slot for unlock_shared().
    token = 0;

    // Fast path
    if (_mutex.read_bias.load(std::memory_order_acquire)) {
      size_t index  = DenseThreadId::self() % SLOT_SIZE;
      Slot  &slot   = _mutex.readers[index];
      bool   expect = false;
      if (slot.mu.compare_exchange_strong(expect, true, std::memory_order_relaxed)) {
        // recheck
        if (_mutex.read_bias.load(std::memory_order_acquire)) {
          token = index + 1;
          return;
        } else {
          slot.mu.store(false, std::memory_order_relaxed);
        }
      }
    }

    // Slow path
    _mutex.underlying.lock_shared();
    if (_mutex.read_bias.load(std::memory_order_acquire) == false && _now() >= _mutex.inhibit_until) {
      _mutex.read_bias.store(true, std::memory_order_release);
    }
  }

  bool
  try_lock_shared(Token &token) TS_TRY_ACQUIRE_SHARED(true) TS_NO_THREAD_SAFETY_ANALYSIS
  {
    debug_assert(SLOT_SIZE >= DenseThreadId::num_possible_values());

    // Clear up front so a slow-path acquisition never leaves a stale fast-path slot for unlock_shared().
    token = 0;

    // Fast path
    if (_mutex.read_bias.load(std::memory_order_acquire)) {
      size_t index  = DenseThreadId::self() % SLOT_SIZE;
      Slot  &slot   = _mutex.readers[index];
      bool   expect = false;

      if (slot.mu.compare_exchange_weak(expect, true, std::memory_order_release, std::memory_order_relaxed)) {
        // recheck
        if (_mutex.read_bias.load(std::memory_order_acquire)) {
          token = index + 1;
          return true;
        } else {
          slot.mu.store(false, std::memory_order_relaxed);
        }
      }
    }

    // Slow path
    bool r = _mutex.underlying.try_lock_shared();
    if (r) {
      // Set RBias if the BRAVO policy allows that
      if (_mutex.read_bias.load(std::memory_order_acquire) == false && _now() >= _mutex.inhibit_until) {
        _mutex.read_bias.store(true, std::memory_order_release);
      }

      return true;
    }

    return false;
  }

  void
  unlock_shared(const Token token) TS_RELEASE_SHARED() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    if (token == 0) {
      _mutex.underlying.unlock_shared();
      return;
    }

    Slot &slot = _mutex.readers[token - 1];
    slot.mu.store(false, std::memory_order_relaxed);
  }

private:
  struct alignas(hardware_constructive_interference_size) Slot {
    std::atomic<bool> mu = false;
  };

  struct Mutex {
    std::atomic<bool>           read_bias = false;
    std::array<Slot, SLOT_SIZE> readers   = {};
    time_point                  inhibit_until{};
    T                           underlying;
  };

  time_point
  _now()
  {
    return std::chrono::system_clock::now();
  }

  /**
     Disable read bias and do revocation
   */
  void
  _revoke()
  {
    if (!_mutex.read_bias.load(std::memory_order_acquire)) {
      // do nothing
      return;
    }

    _mutex.read_bias.store(false, std::memory_order_release);
    time_point start = _now();
    for (size_t i = 0; i < SLOT_SIZE; ++i) {
      for (int j = 0; _mutex.readers[i].mu.load(std::memory_order_relaxed); ++j) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(1 << j));
      }
    }
    time_point n         = _now();
    _mutex.inhibit_until = n + ((n - start) * SLOWDOWN_GUARD);
  }

  ////
  // Variables
  //
  Mutex _mutex;
};

using shared_mutex = shared_mutex_impl<>;

} // namespace ts::bravo
