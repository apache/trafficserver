/** @file

  An AtomicSharedPtr whose readers can take a reference without writing to state shared with
  other threads.

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

namespace ts::detail
{
/// Generations come from one process-wide sequence so that a lease cached for a destroyed instance
/// can never match a new instance created at the same address.
inline uint64_t
next_lease_generation()
{
  static std::atomic<uint64_t> sequence{0};
  return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}
} // namespace ts::detail

/** A shared pointer that is read far more often than it is replaced, by many threads.

    Every load() of an AtomicSharedPtr and every copy of the result write the same cache lines (the
    atomic's lock and the control block's use count) from every thread. load_leased() instead hands
    out references through a control block owned by the calling thread, which in turn holds one
    reference to the shared value. A reader then touches only its own thread's lines plus a
    generation counter that changes only when the value is replaced.

    A thread's control block lives only while references from it exist, so once those are gone the
    shared value's lifetime is the same as with plain load().

    Each thread caches a lease for one instance per @a T; alternating load_leased() calls on two
    instances of the same @a T from one thread work but rebuild the lease each time.
 */
template <class T> class ThreadLeasedSharedPtr
{
public:
  ThreadLeasedSharedPtr() = default;

  ThreadLeasedSharedPtr(const ThreadLeasedSharedPtr &)            = delete;
  ThreadLeasedSharedPtr &operator=(const ThreadLeasedSharedPtr &) = delete;

  std::shared_ptr<T>
  load(std::memory_order order = std::memory_order_seq_cst) const noexcept
  {
    return _ptr.load(order);
  }

  void
  store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    _ptr.store(std::move(desired), order);
    _generation.store(ts::detail::next_lease_generation(), std::memory_order_release);
  }

  std::shared_ptr<T>
  exchange(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    auto previous = _ptr.exchange(std::move(desired), order);
    _generation.store(ts::detail::next_lease_generation(), std::memory_order_release);
    return previous;
  }

  /** The current value, referenced through the calling thread's control block.

      A reader that has not yet observed a concurrent store() may get the previous value, the same
      as a load() that ran just before the store().
   */
  std::shared_ptr<T>
  load_leased() const
  {
    Lease         &lease      = thread_lease();
    uint64_t const generation = _generation.load(std::memory_order_acquire);

    if (lease.owner == this && lease.generation == generation) {
      if (auto value = lease.value.lock(); value) {
        return value;
      }
    }

    // A null value is not cached: there is no reference to share, and the next call rechecks.
    std::shared_ptr<T> shared = _ptr.load(std::memory_order_acquire);
    if (!shared) {
      lease.owner = nullptr;
      return nullptr;
    }
    T *raw = shared.get();
    // libstdc++ destroys a deleter only once the weak count reaches zero, and the lease's weak_ptr
    // keeps it above zero, so the reference must be dropped when the deleter runs.
    std::shared_ptr<T> value(raw, [held = std::move(shared)](T *) mutable { held.reset(); });

    lease.owner      = this;
    lease.generation = generation;
    lease.value      = value;
    return value;
  }

private:
  struct Lease {
    const ThreadLeasedSharedPtr *owner      = nullptr;
    uint64_t                     generation = 0;
    std::weak_ptr<T>             value;
  };

  static Lease &
  thread_lease()
  {
    thread_local Lease lease;
    return lease;
  }

  AtomicSharedPtr<T> _ptr;
  // Read by every load_leased(), so it gets a cache line to itself.
  alignas(64) std::atomic<uint64_t> _generation{ts::detail::next_lease_generation()};
};
