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
#include <string.h>

#if __has_include(<ts/ts.h>)
#include <ts/ts.h>
#else
#include <tscore/Diags.h>
#define TSFatal Fatal
#include <tscore/ink_assert.h>
#define TSAssert ink_assert
#endif

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
class Strerror
{
public:
  Strerror(int err_num)
  {
    // Handle either GNU or XSI version of strerror_r().
    //
    if (!_success(strerror_r(err_num, _buf, 256))) {
      _c_str = "strerror_r() call failed";
    } else {
      _buf[255] = '\0';
      _c_str    = _buf;
    }

    // Make sure there are no unused function warnings.
    //
    static_cast<void>(_success(0));
    static_cast<void>(_success(nullptr));
  }

  char const *
  c_str() const
  {
    return (_c_str);
  }

private:
  char _buf[256];
  char const *_c_str;

  bool
  _success(int retval)
  {
    return retval == 0;
  }

  bool
  _success(char *retval)
  {
    return retval != nullptr;
  }
};

// A class with the same interface as std::shared_mutex, but which is not prone to writer starvation.
//
class shared_mutex
{
public:
  shared_mutex() {}

  // No copying or moving.
  //
  shared_mutex(shared_mutex const &) = delete;
  shared_mutex &operator=(shared_mutex const &) = delete;

  void
  lock()
  {
    int error = pthread_rwlock_wrlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_wrlock", &_lock, error);
    }
    X(_exclusive = true;)
  }

  bool
  try_lock()
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
  unlock()
  {
    X(TSAssert(_exclusive);)
    X(_exclusive = false;)

    _unlock();
  }

  void
  lock_shared()
  {
    int error = pthread_rwlock_rdlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_rdlock", &_lock, error);
    }
    X(TSAssert(_shared >= 0);)
    X(++_shared;)
    X(TSAssert(_shared > 0);)
  }

  bool
  try_lock_shared()
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
  unlock_shared()
  {
    X(TSAssert(_shared > 0);)
    X(--_shared;)
    X(TSAssert(_shared >= 0);)

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
  _unlock()
  {
    int error = pthread_rwlock_unlock(&_lock);
    if (error != 0) {
      _call_fatal("pthread_rwlock_unlock", &_lock, error);
    }
  }

#if defined(linux)
  // Use the initializer that prevents writer starvation.
  //
  pthread_rwlock_t _lock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
#else
  // Testing indicates that for MacOS/Darwin and FreeBSD, pthread rwlocks always prevent writer starvation.
  //
  pthread_rwlock_t _lock = PTHREAD_RWLOCK_INITIALIZER;

#if !(defined(darwin) || defined(freebsd))
#warning "Use of ts::shared_mutex may result in writer starvation"
#endif
#endif

  static void
  _call_fatal(char const *func_name, void *ptr, int errnum)
  {
    TSFatal("%s(%p) failed: %s (%d)", func_name, ptr, Strerror(errnum).c_str(), errnum);
  }

  // In debug builds, make sure shared vs. exlusive locks and unlocks are properly paired.
  //
  X(std::atomic<bool> _exclusive{false};)
  X(std::atomic<int> _shared{0};)
};

} // end namespace ts

#undef X
