/** @file

  Atomic wrapper around std::shared_ptr with the C++20
  std::atomic<std::shared_ptr<T>> API.

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
#include <memory>

// Use the C++20 std::atomic<std::shared_ptr<T>> specialization when the
// standard library provides it, otherwise fall back to the pre-C++20
// std::atomic_*_explicit free-function overloads on shared_ptr.  The
// fallback exists for libstdc++ < 12 and libc++ < 14, which predate the
// specialization.  When those toolchains are no longer supported, delete
// the #else branch and the surrounding #if; call sites do not change.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L

template <class T> using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

#else

// Belt-and-suspenders: on the toolchains that take this branch (libstdc++
// < 12, libc++ < 16) the free-function overloads are not yet marked
// [[deprecated]], so the suppression below is usually a no-op.  It
// matters only if someone forces the fallback on a modern library (e.g.
// -D__cpp_lib_atomic_shared_ptr=0) or compiles against a library that
// ships the deprecation markers ahead of the specialization.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

template <class T> class AtomicSharedPtr
{
public:
  AtomicSharedPtr() noexcept = default;
  AtomicSharedPtr(std::shared_ptr<T> desired) noexcept : _p(std::move(desired)) {}

  AtomicSharedPtr(const AtomicSharedPtr &)            = delete;
  AtomicSharedPtr &operator=(const AtomicSharedPtr &) = delete;

  std::shared_ptr<T>
  load(std::memory_order order = std::memory_order_seq_cst) const noexcept
  {
    return std::atomic_load_explicit(&_p, order);
  }

  void
  store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    std::atomic_store_explicit(&_p, std::move(desired), order);
  }

  std::shared_ptr<T>
  exchange(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept
  {
    return std::atomic_exchange_explicit(&_p, std::move(desired), order);
  }

private:
  std::shared_ptr<T> _p;
};

#pragma GCC diagnostic pop

#endif
