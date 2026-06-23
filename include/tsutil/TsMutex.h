/** @file

  A std::mutex annotated for Clang Thread Safety Analysis, with a matching
  scoped lock guard.

  These are the plain-mutex counterparts to ts::shared_mutex and its
  reader/writer guards (TsSharedMutex.h): use ts::mutex with ts::lock_guard
  wherever you would otherwise use std::mutex with std::lock_guard, but want the
  data it protects checked by -Wthread-safety. The runtime behavior is exactly
  that of std::mutex; the annotations are compile-time only (see
  tsutil/ts_thread_safety.h).

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

#include <mutex>

#include "tsutil/ts_thread_safety.h"

namespace ts
{
// A std::mutex marked as a Clang thread-safety capability, so data guarded by it
// can be checked by -Wthread-safety. Same interface and runtime behavior as
// std::mutex.
//
class TS_CAPABILITY("mutex") mutex
{
public:
  mutex()                         = default;
  mutex(mutex const &)            = delete;
  mutex &operator=(mutex const &) = delete;

  // The lock/unlock bodies are the trusted implementation of this capability:
  // exempt them from analysis so only the capability contract on each signature
  // is checked. The actual data-race checking happens at the call sites.
  void
  lock() TS_ACQUIRE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    _m.lock();
  }
  bool
  try_lock() TS_TRY_ACQUIRE(true) TS_NO_THREAD_SAFETY_ANALYSIS
  {
    return _m.try_lock();
  }
  void
  unlock() TS_RELEASE() TS_NO_THREAD_SAFETY_ANALYSIS
  {
    _m.unlock();
  }

  using native_handle_type = std::mutex::native_handle_type;

  native_handle_type
  native_handle()
  {
    return _m.native_handle();
  }

private:
  std::mutex _m;
};

// RAII guard for ts::mutex that carries Clang thread-safety capability state.
// Prefer over std::lock_guard / std::unique_lock in code annotated for
// -Wthread-safety (see tsutil/ts_thread_safety.h for why the std wrappers are
// not tracked).
//
class TS_SCOPED_CAPABILITY lock_guard
{
public:
  explicit lock_guard(mutex &m) TS_ACQUIRE(m) : _m(m) { _m.lock(); }
  ~lock_guard() TS_RELEASE() { _m.unlock(); }

  lock_guard(lock_guard const &)            = delete;
  lock_guard &operator=(lock_guard const &) = delete;

private:
  mutex &_m;
};

} // end namespace ts
