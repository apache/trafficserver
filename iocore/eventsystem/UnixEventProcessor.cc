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

#include "P_EventSystem.h" /* MAGIC_EDITING_TAG */
#include <sched.h>
#if TS_USE_HWLOC
#if HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <hwloc.h>
#endif
#include "ts/ink_defs.h"
#include "ts/hugepages.h"

/// Global singleton.
class EventProcessor eventProcessor;

class ThreadAffinityInitializer : public Continuation
{
  typedef ThreadAffinityInitializer self;

public:
  /// Default construct.
  ThreadAffinityInitializer() { SET_HANDLER(&self::set_affinity); }
  /// Load up basic affinity data.
  void init();
  /// Set the affinity for the current thread.
  int set_affinity(int, Event *);

#if TS_USE_HWLOC
private:
  hwloc_obj_t _type;
  hwloc_obj_type_t obj_type;
  int obj_count;
  char const *obj_name;
#endif
};

ThreadAffinityInitializer Thread_Affinity_Initializer;

namespace
{
class ThreadInitByFunc : public Continuation
{
public:
  ThreadInitByFunc() { SET_HANDLER(&ThreadInitByFunc::invoke); }
  int
  invoke(int, Event *ev)
  {
    void (*f)(EThread *) = reinterpret_cast<void (*)(EThread *)>(ev->cookie);
    f(ev->ethread);
    return 0;
  }
} Thread_Init_Func;
}

void
ThreadAffinityInitializer::init()
{
#if TS_USE_HWLOC
  int affinity = 1;
  REC_ReadConfigInteger(affinity, "proxy.config.exec_thread.affinity");

  switch (affinity) {
  case 4: // assign threads to logical processing units
// Older versions of libhwloc (eg. Ubuntu 10.04) don't have HWLOC_OBJ_PU.
#if HAVE_HWLOC_OBJ_PU
    obj_type = HWLOC_OBJ_PU;
    obj_name = "Logical Processor";
    break;
#endif
  case 3: // assign threads to real cores
    obj_type = HWLOC_OBJ_CORE;
    obj_name = "Core";
    break;
  case 1: // assign threads to NUMA nodes (often 1:1 with sockets)
    obj_type = HWLOC_OBJ_NODE;
    obj_name = "NUMA Node";
    if (hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type) > 0) {
      break;
    }
  case 2: // assign threads to sockets
    obj_type = HWLOC_OBJ_SOCKET;
    obj_name = "Socket";
    break;
  default: // assign threads to the machine as a whole (a level below SYSTEM)
    obj_type = HWLOC_OBJ_MACHINE;
    obj_name = "Machine";
  }

  obj_count = hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type);
  Debug("iocore_thread", "Affinity: %d %ss: %d PU: %d", affinity, obj_name, obj_count, ink_number_of_processors());
#endif
}

int
ThreadAffinityInitializer::set_affinity(int, Event *)
{
#if TS_USE_HWLOC
  hwloc_obj_t obj;
  EThread *t = this_ethread();

  if (obj_count > 0) {
    obj = hwloc_get_obj_by_type(ink_get_topology(), obj_type, t->id % obj_count);
#if HWLOC_API_VERSION >= 0x00010100
    int cpu_mask_len = hwloc_bitmap_snprintf(NULL, 0, obj->cpuset) + 1;
    char *cpu_mask   = (char *)alloca(cpu_mask_len);
    hwloc_bitmap_snprintf(cpu_mask, cpu_mask_len, obj->cpuset);
    Debug("iocore_thread", "EThread: %p %s: %d CPU Mask: %s\n", t, obj_name, obj->logical_index, cpu_mask);
#else
    Debug("iocore_thread", "EThread: %d %s: %d", _name, obj->logical_index);
#endif // HWLOC_API_VERSION
    hwloc_set_thread_cpubind(ink_get_topology(), t->tid, obj->cpuset, HWLOC_CPUBIND_STRICT);
  } else {
    Warning("hwloc returned an unexpected number of objects -- CPU affinity disabled");
  }
#endif // TS_USE_HWLOC
  return 0;
}

EventProcessor::EventProcessor() : thread_initializer(this)
{
  ink_zero(all_ethreads);
  ink_zero(all_dthreads);
  ink_zero(thread_group);
  ink_mutex_init(&dedicated_thread_spawn_mutex);
  // Because ET_NET is compile time set to 0 it *must* be the first type registered.
  this->register_event_type("ET_NET");
}

EventProcessor::~EventProcessor()
{
  ink_mutex_destroy(&dedicated_thread_spawn_mutex);
}

namespace
{
Event *
make_event_for_scheduling(Continuation *c, int event_code, void *cookie)
{
  Event *e = eventAllocator.alloc();

  e->init(c);
  e->mutex          = c->mutex;
  e->callback_event = event_code;
  e->cookie         = cookie;

  return e;
}
}

Event *
EventProcessor::schedule_spawn(Continuation *c, EventType ev_type, int event_code, void *cookie)
{
  Event *e = make_event_for_scheduling(c, event_code, cookie);
  ink_assert(ev_type < MAX_EVENT_TYPES);
  thread_group[ev_type]._spawnQueue.enqueue(e);
  return e;
}

Event *
EventProcessor::schedule_spawn(void (*f)(EThread *), EventType ev_type)
{
  Event *e = make_event_for_scheduling(&Thread_Init_Func, EVENT_IMMEDIATE, reinterpret_cast<void *>(f));
  ink_assert(ev_type < MAX_EVENT_TYPES);
  thread_group[ev_type]._spawnQueue.enqueue(e);
  return e;
}

EventType
EventProcessor::register_event_type(char const *name)
{
  ThreadGroupDescriptor *tg = &(thread_group[n_thread_groups++]);
  ink_release_assert(n_thread_groups <= MAX_EVENT_TYPES); // check for overflow

  tg->_name = ats_strdup(name);
  return n_thread_groups - 1;
}

EventType
EventProcessor::spawn_event_threads(char const *name, int n_threads, size_t stacksize)
{
  int ev_type = this->register_event_type(name);
  this->spawn_event_threads(ev_type, n_threads, stacksize);
  return ev_type;
}

EventType
EventProcessor::spawn_event_threads(EventType ev_type, int n_threads, size_t stacksize)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  int i;
  ThreadGroupDescriptor *tg = &(thread_group[ev_type]);

  ink_release_assert(n_threads > 0);
  ink_release_assert((n_ethreads + n_threads) <= MAX_EVENT_THREADS);
  ink_release_assert(ev_type < MAX_EVENT_TYPES);

  for (i = 0; i < n_threads; ++i) {
    EThread *t                   = new EThread(REGULAR, n_ethreads + i);
    all_ethreads[n_ethreads + i] = t;
    tg->_thread[i]               = t;
    t->set_event_type(ev_type);
    t->schedule_spawn(&thread_initializer);
  }
  tg->_count = n_threads;

  for (i = 0; i < n_threads; i++) {
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[%s %d]", tg->_name.get(), i);
    tg->_thread[i]->start(thr_name, nullptr, stacksize);
  }

  n_ethreads += n_threads;
  Debug("iocore_thread", "Created thread group '%s' id %d with %d threads", tg->_name.get(), ev_type, n_threads);

  return ev_type; // useless but not sure what would be better.
}

// This is called from inside a thread as the @a start_event for that thread.  It chains to the
// startup events for the appropriate thread group start events.
void
EventProcessor::initThreadState(EThread *t)
{
  // Run all thread type initialization continuations that match the event types for this thread.
  for (int i = 0; i < MAX_EVENT_TYPES; ++i) {
    if (t->is_event_type(i)) { // that event type done here, roll thread start events of that type.
      // To avoid race conditions on the event in the spawn queue, create a local one to actually send.
      // Use the spawn queue event as a read only model.
      Event *nev = eventAllocator.alloc();
      for (Event *ev = thread_group[i]._spawnQueue.head; NULL != ev; ev = ev->link.next) {
        nev->init(ev->continuation, 0, 0);
        nev->ethread        = t;
        nev->callback_event = ev->callback_event;
        nev->mutex          = ev->continuation->mutex;
        nev->cookie         = ev->cookie;
        ev->continuation->handleEvent(ev->callback_event, nev);
      }
      nev->free();
    }
  }
}

int
EventProcessor::start(int n_event_threads, size_t stacksize)
{
  // do some sanity checking.
  static bool started = false;
  ink_release_assert(!started);
  ink_release_assert(n_event_threads > 0 && n_event_threads <= MAX_EVENT_THREADS);
  started = true;

  Thread_Affinity_Initializer.init();
  // Least ugly thing - this needs to be the first callback from the thread but by the time this
  // method is called other spawn callbacks have been registered. This forces thread affinity
  // first. The other alternative would be to require a call to an @c init method which I like even
  // less because this cannot be done in the constructor - that depends on too much other
  // infrastructure being in place (e.g. the proxy allocators).
  thread_group[ET_CALL]._spawnQueue.push(make_event_for_scheduling(&Thread_Affinity_Initializer, EVENT_IMMEDIATE, nullptr));

  this->spawn_event_threads(ET_CALL, n_event_threads, stacksize);

  Debug("iocore_thread", "Created event thread group id %d with %d threads", ET_CALL, n_event_threads);
  return 0;
}

void
EventProcessor::shutdown()
{
}

Event *
EventProcessor::spawn_thread(Continuation *cont, const char *thr_name, size_t stacksize)
{
  /* Spawning threads in a live system - There are two potential race conditions in this logic. The
     first is multiple calls to this method.  In that case @a all_dthreads can end up in a bad state
     as the same entry is overwritten while another is left uninitialized.

     The other is read/write contention where another thread (e.g. the stats collection thread) is
     iterating over the threads while the active count (@a n_dthreads) is being updated causing use
     of a not yet initialized array element.

     This logic covers both situations. For write/write the actual array update is locked. The
     potentially expensive set up is done outside the lock making the time spent locked small. For
     read/write it suffices to do the active count increment after initializing the array
     element. It's not a problem if, for one cycle, a new thread is skipped.
  */

  // Do as much as possible outside the lock. Until the array element and count is changed
  // this is thread safe.
  Event *e = eventAllocator.alloc();
  e->init(cont, 0, 0);
  e->ethread  = new EThread(DEDICATED, e);
  e->mutex    = e->ethread->mutex;
  cont->mutex = e->ethread->mutex;
  {
    ink_scoped_mutex_lock lock(dedicated_thread_spawn_mutex);
    ink_release_assert(n_dthreads < MAX_EVENT_THREADS);
    all_dthreads[n_dthreads] = e->ethread;
    ++n_dthreads; // Be very sure this is after the array element update.
  }

  e->ethread->start(thr_name, nullptr, stacksize);

  return e;
}
