/** @file

  A brief file description

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

  Continuations have a handleEvent() method to invoke them. Users
  can determine the behavior of a Continuation by supplying a
  "ContinuationHandler" (member function name) which is invoked
  when events arrive. This function can be changed with the
  "setHandler" method.

  Continuations can be subclassed to add additional state and
  methods.

 */

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "I_Lock.h"
#include "tscore/ContFlags.h"

class Continuation;
class ContinuationQueue;
class Processor;
class ProxyMutex;
class EThread;
class Event;

extern EThread *this_ethread();
extern EThread *this_event_thread();

//////////////////////////////////////////////////////////////////////////////
//
//  Constants and Type Definitions
//
//////////////////////////////////////////////////////////////////////////////

#define CONTINUATION_EVENT_NONE 0

#define CONTINUATION_DONE 0
#define CONTINUATION_CONT 1

typedef int (Continuation::*ContinuationHandler)(int event, void *data);

// Convert event handler pointer fp to type ContinuationHandler, but with a compiler error if class C is not
// derived from the class Continuation.
//
template <class C, typename T>
constexpr ContinuationHandler
continuation_handler_void_ptr(int (C::*fp)(int, T *))
{
  auto fp2 = reinterpret_cast<int (C::*)(int, void *)>(fp);
  return static_cast<ContinuationHandler>(fp2);
}

// Overload for nullptr.
//
constexpr ContinuationHandler continuation_handler_void_ptr(std::nullptr_t)
{
#undef X
#if !defined(__GNUC__)
#define X 1
#else
#define X (__GNUC__ > 7)
#endif
#if X
  static_assert(!static_cast<ContinuationHandler>(nullptr));
#endif
#undef X

  return static_cast<ContinuationHandler>(nullptr);
}

class force_VFPT_to_top
{
public:
  virtual ~force_VFPT_to_top() {}
};

/**
  Base class for all state machines to receive notification of
  events.

  The Continuation class represents the main abstraction mechanism
  used throughout the IO Core Event System to communicate its users
  the occurrence of an event. A Continuation is a lightweight data
  structure that implements a single method with which the user is
  called back.

  Continuations are typically subclassed in order to implement
  event-driven state machines. By including additional state and
  methods, continuations can combine state with control flow, and
  they are generally used to support split-phase, event-driven
  control flow.

  Given the multithreaded nature of the Event System, every
  continuation carries a reference to a ProxyMutex object to protect
  its state and ensure atomic operations. This ProxyMutex object
  must be allocated by continuation-derived classes or by clients
  of the IO Core Event System and it is required as a parameter to
  the Continuation's class constructor.

*/

class Continuation : private force_VFPT_to_top
{
public:
  /**
    The current continuation handler function.

    The current handler should not be set directly. In order to
    change it, first acquire the Continuation's lock and then use
    the SET_HANDLER macro which takes care of the type casting
    issues.

  */
  ContinuationHandler handler = nullptr;

#ifdef DEBUG
  const char *handler_name = nullptr;
#endif

  /**
    The Continuation's lock.

    A reference counted pointer to the Continuation's lock. This
    lock is initialized in the constructor and should not be set
    directly.

    TODO:  make this private.

  */
  Ptr<ProxyMutex> mutex;

  ProxyMutex *
  getMutex() const
  {
    return mutex.get();
  }

  /**
    Link to other continuations.

    A doubly-linked element to allow Lists of Continuations to be
    assembled.

  */
  LINK(Continuation, link);

  /**
    Contains values for debug_override and future flags that
    needs to be thread local while this continuation is running
  */
  ContFlags control_flags;

  EThread *thread_affinity = nullptr;

  bool
  setThreadAffinity(EThread *ethread)
  {
    if (ethread != nullptr) {
      thread_affinity = ethread;
      return true;
    }
    return false;
  }

  EThread *
  getThreadAffinity()
  {
    return thread_affinity;
  }

  void
  clearThreadAffinity()
  {
    thread_affinity = nullptr;
  }

  /**
    Receives the event code and data for an Event.

    This function receives the event code and data for an event and
    forwards them to the current continuation handler. The processor
    calling back the continuation is responsible for acquiring its
    lock.  If the lock is present and not held, this method will assert.

    @param event Event code to be passed at callback (Processor specific).
    @param data General purpose data related to the event code (Processor specific).
    @return State machine and processor specific return code.

  */
  TS_INLINE int
  handleEvent(int event = CONTINUATION_EVENT_NONE, void *data = nullptr)
  {
    // If there is a lock, we must be holding it on entry
    ink_release_assert(!mutex || mutex->thread_holding == this_ethread());
    return (this->*handler)(event, data);
  }

protected:
  /**
    Constructor of the Continuation object. It should not be used
    directly. Instead create an object of a derived type.

    @param amutex Lock to be set for this Continuation.

  */
  explicit Continuation(ProxyMutex *amutex = nullptr);
  explicit Continuation(Ptr<ProxyMutex> &amutex);
};

/**
  Sets the Continuation's handler. The preferred mechanism for
  setting the Continuation's handler.

  @param _h Pointer to the function used to callback with events.

*/
#ifdef DEBUG
#define SET_HANDLER(_h) (handler = continuation_handler_void_ptr(_h), handler_name = #_h)
#else
#define SET_HANDLER(_h) (handler = continuation_handler_void_ptr(_h))
#endif

/**
  Sets a Continuation's handler.

  The preferred mechanism for setting the Continuation's handler.

  @param _c Pointer to a Continuation whose handler is being set.
  @param _h Pointer to the function used to callback with events.

*/
#ifdef DEBUG
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = continuation_handler_void_ptr(_h), _c->handler_name = #_h)
#else
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = continuation_handler_void_ptr(_h))
#endif

inline Continuation::Continuation(Ptr<ProxyMutex> &amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}

inline Continuation::Continuation(ProxyMutex *amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}
