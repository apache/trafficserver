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
  using Action = TSAction;

  // Create continuation, mutexp may be nullptr.
  //
  explicit Continuation(TSMutex mutexp) : _cont(TSContCreate(_generalEventFunc, mutexp))
  {
    TSContDataSet(_cont, static_cast<void *>(this));
  }

  // Create "empty" continuation, can only be populated by move assignement.
  //
  Continuation() {}

  class Mutex
  {
  public:
    Mutex() : _mutexp(nullptr) {}

    // Call this from TSRemapInit() or TSPluginInit() function.
    //
    void
    init()
    {
      if (!_mutexp) {
        _mutexp = TSMutexCreate();
      }
    }

    TSMutex
    asTSMutex() const
    {
      return _mutexp;
    }

    // No copying.
    //
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

    // Moving allowed.
    //
    Mutex(Mutex &&that)
    {
      _mutexp      = that._mutexp;
      that._mutexp = nullptr;
    }
    Mutex &
    operator=(Mutex &&that)
    {
      if (&that != this) {
        if (_mutexp) {
          // For now, this cannot be called, due to Issue #4854
          // TSMutexDestroy(_mutexp);
        }
        _mutexp      = that._mutexp;
        that._mutexp = nullptr;
      }
      return *this;
    }

    ~Mutex()
    {
      if (_mutexp) {
        // For now, this cannot be called, due to Issue #4854
        // TSMutexDestroy(_mutexp);
      }
    }

  private:
    TSMutex _mutexp;
  };

  explicit Continuation(const Mutex &mutex) : Continuation(mutex.asTSMutex()) {}

  TSCont
  asTSCont() const
  {
    return _cont;
  }

  bool
  isNull() const
  {
    return _cont == nullptr;
  }

  // Get mutex (for "non-empty" continuation).
  //
  TSMutex
  getTSMutex()
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
  //
  Continuation(const Continuation &) = delete;
  Continuation &operator=(const Continuation &) = delete;

  // Moving allowed.
  //
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

  // Delay of zero means no delay (schedule immediate).
  //
  Action
  schedule(TSHRTime delay = 0)
  {
    return TSContSchedule(_cont, delay);
  }

  // Delay of zero means no delay.
  //
  Action
  httpSchedule(TSHttpTxn httpTransactionp, TSHRTime delay = 0)
  {
    return TSHttpSchedule(_cont, httpTransactionp, delay);
  }

  Action
  scheduleEvery(TSHRTime interval /* milliseconds */, TSThreadPool tp = TS_THREAD_POOL_NET)
  {
    return TSContScheduleEveryOnPool(_cont, interval, tp);
  }

protected:
  // Distinct continuation behavior is acheived by overriding this function in a derived continutation type.
  //
  virtual int _run(TSEvent event, void *edata) = 0;

  // This is the event function for all continuations in C++ plugins.
  //
  static int _generalEventFunc(TSCont cont, TSEvent event, void *edata);

  TSCont _cont = nullptr;
};

// Continue by calling a member function of a given instance of a class.
//
// One use-case is a function that would "naturally" be written like:
//
// void func()
// {
//   // Before critical section
//
//   // Lock mutex
//
//   // Critical section
//
//   // Release mutex
//
//   // After critical section
// }
//
// But in traffic server this is generally not acceptable.  Waiting to lock a mutex in an event thread also
// blocks the handling of all the events queued on that thread.  The function can be changed into a functor:
//
// class Func
// {
// public:
//   void operator()()
//   {
//     // Before critical section
//
//     ContinueInMemberFunc<Func, &Func::critical>::once(this, mutex)->schedule();
//   }
//
// private:
//   void critical(TSEvent, void *)
//   {
//     // Critical section
//
//     ContinueInMemberFunc<Func, &Func::after>once(this, nullptr)->schedule();
//   }
//
//   void after(TSEvent, void *)
//   {
//      // After critical section
//
//      // Typically such an object would clean itself up.
//      delete this;
//   }
//
//   // Local variables of the original function would become member variables of the class as needed.
// };
//
template <class C, int (C::*MemberFunc)(TSEvent, void *edata)> class ContinueInMemberFunc final : public Continuation
{
public:
  ContinueInMemberFunc(C &inst, TSMutex mutexp) : Continuation(mutexp), _inst(inst){};

  ContinueInMemberFunc(C &inst, const Mutex &mutex) : ContinueInMemberFunc(inst, mutex.asTSMutex()){};

  // Returns an instance of the Continuation class that is dynamically allocated and will delete itself after being
  // triggered.
  //
  static Continuation *
  once(C &inst, TSMutex mutexp)
  {
    return new Once(inst, mutexp);
  }

  // Returns an instance of the Continuation class that is dynamically allocated and will delete itself after being
  // triggered.
  //
  static Continuation *
  once(C &inst, const Mutex &mutex)
  {
    return new Once(inst, mutex.asTSMutex());
  }

private:
  int
  _run(TSEvent event, void *edata) final
  {
    return (_inst.*MemberFunc)(event, edata);
  }

  C &_inst;

  class Once final : public Continuation
  {
  public:
    Once(C &inst, TSMutex mutexp) : Continuation(mutexp), _inst(inst){};

  private:
    int
    _run(TSEvent event, void *edata) final
    {
      int result = (_inst.*MemberFunc)(event, edata);
      delete this;
      return result;
    }

    C &_inst;
  };
};

} // end namespace atscppapi
