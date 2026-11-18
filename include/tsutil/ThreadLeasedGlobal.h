/** @file

  A process-wide AtomicSharedPtr whose readers can take a reference without writing to state
  shared with other threads.

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

#include "tsutil/AtomicSharedPtr.h"

#include <atomic>
#include <cstdint>
#include <memory>

/** A process-wide shared pointer that is read far more often than it is replaced, by many threads.

    Every load() of an AtomicSharedPtr and every copy of the result write the same cache lines (the
    atomic's lock and the control block's use count) from every thread. load_leased() instead hands
    out references through a control block owned by the calling thread, which in turn holds one
    reference to the shared value. A reader then touches only its own thread's lines plus a
    generation counter that changes only when the value is replaced.

    The thread's reference to the shared value ends with the last reference handed out through its
    control block, so once those are gone the shared value's lifetime is the same as with plain
    load().

    Each thread caches one lease per (@a T, @a Tag), so the class has only static members and there
    is exactly one value per (@a T, @a Tag). Use a distinct @a Tag for each independent value.
 */
template <class T, class Tag = T> class ThreadLeasedGlobal
{
public:
  ThreadLeasedGlobal() = delete;

  static std::shared_ptr<T>
  load(std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    return _ptr.load(order);
  }

  static void
  store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    _ptr.store(std::move(desired), order);
    bump_generation();
  }

  static std::shared_ptr<T>
  exchange(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    auto previous = _ptr.exchange(std::move(desired), order);
    bump_generation();
    return previous;
  }

  /** The current value, referenced through the calling thread's control block.

      A reader that has not yet observed a concurrent store() may get the previous value, the same
      as a load() that ran just before the store().
   */
  static std::shared_ptr<T>
  load_leased()
  {
    uint64_t const generation = _generation.value.load(std::memory_order_acquire);

    if (_lease.generation == generation) {
      if (auto value = _lease.value.lock(); value) {
        return value;
      }
    }

    // A null value is not cached: there is no reference to share, and the next call rechecks.
    std::shared_ptr<T> shared = _ptr.load(std::memory_order_acquire);
    if (!shared) {
      return nullptr;
    }
    T *raw = shared.get();
    // libstdc++ destroys a deleter only once the weak count reaches zero, and the lease's weak_ptr
    // keeps it above zero, so the reference must be dropped when the deleter runs.
    std::shared_ptr<T> value(raw, [held = std::move(shared)](T *) mutable { held.reset(); });

    _lease.generation = generation;
    _lease.value      = value;
    return value;
  }

private:
  struct Lease {
    uint64_t         generation = 0;
    std::weak_ptr<T> value;
  };

  // A read-modify-write, so that when stores overlap, a reader that acquires the final generation
  // also sees the last pointer stored by every one of them.
  static void
  bump_generation() noexcept
  {
    _generation.value.fetch_add(1, std::memory_order_release);
  }

  static inline constinit AtomicSharedPtr<T> _ptr;
  // Read by every load_leased(). Aligning the type rather than the variable pads it to a whole cache
  // line, so the linker cannot place other globals beside it.
  struct alignas(64) Generation {
    std::atomic<uint64_t> value{0};
  };
  static_assert(sizeof(Generation) == 64);

  static inline constinit Generation _generation;
  static inline thread_local Lease   _lease;
};
