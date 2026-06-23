/** @file

  A drop-in replacement for std::shared_mutex with guarantees against writer
  starvation.

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

#include <pthread.h>
#include "tsutil/Strerror.h"
#include "tsutil/Assert.h"
#include "tsutil/ts_thread_safety.h"

#ifdef X
#error "X preprocessor symbol defined"
#endif

#if !defined(__OPTIMIZE__)

#define X(P) P

#include <atomic>

#else

#define X(P)

#endif

namespace ts
{
// A class with the same interface as std::shared_mutex, but which is not prone to writer starvation.
//
class TS_CAPABILITY("shared_mutex") shared_mutex
{
public:
  shared_mutex() {}

  // No copying or moving.
  //
  shared_mutex(shared_mutex const &)            = delete;
  shared_mutex &operator=(shared_mutex const &) = delete;

  // The lock/unlock methods are the trusted implementation of this capability:
  // their bodies drive the raw pthread_rwlock_t, which some libc headers (e.g.
  // FreeBSD's <pthread.h>) annotate as a capability in its own right. Exempt the
  // bodies from analysis so only the capability contract on each signature is
  // checked; the actual data-race checking happens at the call sites.
  void
  lock() TS_ACQUIRE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    int error = pthread_rwlock_wrlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_wrlock", &_lock, error);
    }
    X(_exclusive = true;)
  }

  bool
  try_lock() TS_TRY_ACQUIRE(true) TS_NO_THREAD_SAFETY_ANALYSIS
  {
    int error = pthread_rwlock_trywrlock(&_lock);
    if (EBUSY == error) {
      return false;
    }
    if (error != 0) {
      _call_fatal("pthread_rwlock_trywrlock", &_lock, error);
    }
    X(_exclusive = true;)

    return true;
  }

  void
  unlock() TS_RELEASE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    X(debug_assert(_exclusive);)
    X(_exclusive = false;)

    _unlock();
  }

  void
  lock_shared() TS_ACQUIRE_SHARED() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    int error = pthread_rwlock_rdlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_rdlock", &_lock, error);
    }
    X(debug_assert(_shared >= 0);)
    X(++_shared;)
    X(debug_assert(_shared > 0);)
  }

  bool
  try_lock_shared() TS_TRY_ACQUIRE_SHARED(true) TS_NO_THREAD_SAFETY_ANALYSIS
  {
    int error = pthread_rwlock_tryrdlock(&_lock);
    if (EBUSY == error) {
      return false;
    }
    if (error != 0) {
      _call_fatal("pthread_rwlock_tryrdlock", &_lock, error);
    }

    X(++_shared;)

    return true;
  }

  void
  unlock_shared() TS_RELEASE_SHARED() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    X(debug_assert(_shared > 0);)
    X(--_shared;)
    X(debug_assert(_shared >= 0);)

    _unlock();
  }

  ~shared_mutex()
  {
    int error = pthread_rwlock_destroy(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_destroy", &_lock, error);
    }
  }

  using native_handle_type = pthread_rwlock_t;

  native_handle_type
  native_handle()
  {
    return _lock;
  }

private:
  void
  _unlock() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    int error = pthread_rwlock_unlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_unlock", &_lock, error);
    }
  }

#if defined(__linux__)
  // Use the initializer that prevents writer starvation.
  //
  pthread_rwlock_t _lock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
#else
  // Testing indicates that for MacOS/Darwin and FreeBSD, pthread rwlocks always prevent writer starvation.
  //
  pthread_rwlock_t _lock = PTHREAD_RWLOCK_INITIALIZER;
#endif

  static void
  _call_fatal(char const *func_name, void *ptr, int errnum)
  {
    fatal_error("{}({}) failed: {} ({})", func_name, ptr, Strerror(errnum).c_str(), errnum);
  }

  // In debug builds, make sure shared vs. exclusive locks and unlocks are properly paired.
  //
  X(std::atomic<bool> _exclusive{false};)
  X(std::atomic<int> _shared{0};)
};

// RAII guards for ts::shared_mutex that carry Clang thread-safety capability
// state. Prefer these over std::unique_lock / std::shared_lock in code annotated
// for -Wthread-safety: the analysis does not reliably track the std wrappers
// (see tsutil/ts_thread_safety.h).
//
class TS_SCOPED_CAPABILITY write_guard
{
public:
  explicit write_guard(shared_mutex &m) TS_ACQUIRE(m) : _m(m) { _m.lock(); }
  ~write_guard() TS_RELEASE() { _m.unlock(); }

  write_guard(write_guard const &)            = delete;
  write_guard &operator=(write_guard const &) = delete;

private:
  shared_mutex &_m;
};

class TS_SCOPED_CAPABILITY read_guard
{
public:
  explicit read_guard(shared_mutex &m) TS_ACQUIRE_SHARED(m) : _m(m) { _m.lock_shared(); }
  // A scoped-capability destructor uses the plain release form even for a shared acquire.
  ~read_guard() TS_RELEASE() { _m.unlock_shared(); }

  read_guard(read_guard const &)            = delete;
  read_guard &operator=(read_guard const &) = delete;

private:
  shared_mutex &_m;
};

} // end namespace ts

#undef X
