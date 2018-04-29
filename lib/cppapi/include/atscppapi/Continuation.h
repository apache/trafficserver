/**
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

/**
 * @file Continuation.h
 * @brief Wrapper class for TS API type TSCont.
 */

#pragma once

#include <ts/ts.h>

namespace atscppapi
{
class Continuation
{
public:
  using Mutex = TSMutex;

  using Action = TSAction;

  // Create continuation, mutexp may be nullptr.
  //
  explicit Continuation(Mutex mutexp) : _cont(TSContCreate(_generalEventFunc, mutexp))
  {
    TSContDataSet(_cont, static_cast<void *>(this));
  }

  // Create "empty" continuation, can only be populated by move assignement.
  //
  Continuation() : _cont(nullptr) {}

  TSCont
  asTSCont() const
  {
    return _cont;
  }

  // Get mutex (for "non-empty" continuation).
  //
  Mutex
  mutex()
  {
    return _cont ? TSContMutexGet(_cont) : nullptr;
  }

  void
  destroy()
  {
    if (_cont) {
      TSContDestroy(_cont);
      _cont = nullptr;
    }
  }

  virtual ~Continuation()
  {
    if (_cont) {
      TSContDestroy(_cont);
    }
  }

  // No copying.
  Continuation(const Continuation &) = delete;
  Continuation &operator=(const Continuation &) = delete;

  // Moving allowed.
  Continuation(Continuation &&that)
  {
    _cont      = that._cont;
    that._cont = nullptr;
    TSContDataSet(_cont, static_cast<void *>(this));
  }
  Continuation &
  operator=(Continuation &&that)
  {
    if (&that != this) {
      if (_cont) {
        TSContDestroy(_cont);
      }
      _cont      = that._cont;
      that._cont = nullptr;
      TSContDataSet(_cont, static_cast<void *>(this));
    }
    return *this;
  }

  explicit operator bool() const { return _cont != nullptr; }

  int
  call(TSEvent event, void *edata = nullptr)
  {
    return TSContCall(_cont, event, edata);
  }

  // Timeout of zero means no timeout.
  //
  Action
  schedule(TSHRTime timeout = 0, TSThreadPool tp = TS_THREAD_POOL_DEFAULT)
  {
    return TSContSchedule(_cont, timeout, tp);
  }

  // Timeout of zero means no timeout.
  //
  Action
  httpSchedule(TSHttpTxn httpTransactionp, TSHRTime timeout = 0)
  {
    return TSHttpSchedule(_cont, httpTransactionp, timeout);
  }

  Action
  scheduleEvery(TSHRTime interval /* milliseconds */, TSThreadPool tp = TS_THREAD_POOL_DEFAULT)
  {
    return TSContScheduleEvery(_cont, interval, tp);
  }

protected:
  // Distinct continuation behavior is acheived by overriding this function in a derived continutation type.
  //
  virtual int _run(TSEvent event, void *edata) = 0;

  // This is the event function for all continuations in C++ plugins.
  //
  static int _generalEventFunc(TSCont cont, TSEvent event, void *edata);

  TSCont _cont;
};

} // end namespace atscppapi
