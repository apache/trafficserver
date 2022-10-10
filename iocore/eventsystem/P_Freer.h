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
 */

#pragma once

#include "tscore/ink_platform.h"
#include "I_Tasks.h"

// Note that these should not be used for memory that wishes to retain
// NUMA socket affinity. We'll potentially return these on an arbitrarily
// selected processor/socket.

template <class C> struct DeleterContinuation : public Continuation {
public: // Needed by WinNT compiler (compiler bug)
  C *p;
  int
  dieEvent(int event, void *e)
  {
    (void)event;
    (void)e;
    if (p) {
      delete p;
    }
    delete this;
    return EVENT_DONE;
  }
  explicit DeleterContinuation(C *ap) : Continuation(new_ProxyMutex()), p(ap) { SET_HANDLER(&DeleterContinuation::dieEvent); }
};

// This can be useful for two things (or both):
//    1. Make sure to schedule a delete on an ET_TASK thread
//    2. Delay the delete (this should be used sparingly)
template <class C>
TS_INLINE void
new_Deleter(C *ap, ink_hrtime t)
{
  if (t > 0) {
    eventProcessor.schedule_in(new DeleterContinuation<C>(ap), t, ET_TASK);
  } else {
    eventProcessor.schedule_imm(new DeleterContinuation<C>(ap), ET_TASK);
  }
}

template <class C> struct FreeCallContinuation : public Continuation {
public: // Needed by WinNT compiler (compiler bug)
  C *p;
  int
  dieEvent(int event, void *e)
  {
    (void)event;
    (void)e;
    p->free();
    delete this;
    return EVENT_DONE;
  }
  explicit FreeCallContinuation(C *ap) : Continuation(nullptr), p(ap) { SET_HANDLER(&FreeCallContinuation::dieEvent); }
};

template <class C>
TS_INLINE void
new_FreeCaller(C *ap, ink_hrtime t)
{
  eventProcessor.schedule_in(new FreeCallContinuation<C>(ap), t, ET_TASK);
}

struct FreerContinuation;
typedef int (FreerContinuation::*FreerContHandler)(int, void *);

struct FreerContinuation : public Continuation {
  void *p;

  int
  dieEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    ats_free(p);
    delete this;
    return EVENT_DONE;
  }

  explicit FreerContinuation(void *ap) : Continuation(nullptr), p(ap) { SET_HANDLER(&FreerContinuation::dieEvent); }
};

TS_INLINE void
new_Freer(void *ap, ink_hrtime t)
{
  eventProcessor.schedule_in(new FreerContinuation(ap), t, ET_TASK);
}

template <class C> struct DereferContinuation : public Continuation {
  C *p;

  int
  dieEvent(int, Event *)
  {
    p->refcount_dec();
    if (p->refcount_dec() == 0) {
      delete p;
    }

    delete this;
    return EVENT_DONE;
  }

  explicit DereferContinuation(C *ap) : Continuation(nullptr), p(ap) { SET_HANDLER(&DereferContinuation::dieEvent); }
};

template <class C>
TS_INLINE void
new_Derefer(C *ap, ink_hrtime t)
{
  eventProcessor.schedule_in(new DereferContinuation<C>(ap), t, ET_TASK);
}
