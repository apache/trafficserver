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

#include "P_EventSystem.h"
#include <sched.h>
#if TS_USE_HWLOC
#if HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <hwloc.h>
#endif
#include "tscore/ink_defs.h"
#include "tscore/hugepages.h"

/// Global singleton.
class EventProcessor eventProcessor;

class ThreadAffinityInitializer : public Continuation
{
  using self = ThreadAffinityInitializer;

public:
  /// Default construct.
  ThreadAffinityInitializer() { SET_HANDLER(&self::set_affinity); }
  /// Load up basic affinity data.
  void init();
  /// Set the affinity for the current thread.
  int set_affinity(int, Event *);
  /// Allocate a stack.
  /// @internal This is the external entry point and is different depending on
  /// whether HWLOC is enabled.
  void *alloc_stack(EThread *t, size_t stacksize);

protected:
  /// Allocate a hugepage stack.
  /// If huge pages are not enable, allocate a basic stack.
  void *alloc_hugepage_stack(size_t stacksize);

#if TS_USE_HWLOC

  /// Allocate a stack based on NUMA information, if possible.
  void *alloc_numa_stack(EThread *t, size_t stacksize);

private:
  hwloc_obj_type_t obj_type = HWLOC_OBJ_MACHINE;
  int obj_count             = 0;
  char const *obj_name      = nullptr;
#endif
};

ThreadAffinityInitializer Thread_Affinity_Initializer;

namespace
{
int
EventMetricStatSync(const char *, RecDataT, RecData *, RecRawStatBlock *rsb, int)
{
  int id = 0;
  EThread::EventMetrics summary[EThread::N_EVENT_TIMESCALES];

  // scan the thread local values
  for (EThread *t : eventProcessor.active_group_threads(ET_CALL)) {
    t->summarize_stats(summary);
  }

  ink_mutex_acquire(&(rsb->mutex));

  for (int ts_idx = 0; ts_idx < EThread::N_EVENT_TIMESCALES; ++ts_idx, id += EThread::N_EVENT_STATS) {
    EThread::EventMetrics *m = summary + ts_idx;
    // Discarding the atomic swaps for global writes, doesn't seem to actually do anything useful.
    rsb->global[id + EThread::STAT_LOOP_COUNT]->sum   = m->_count;
    rsb->global[id + EThread::STAT_LOOP_COUNT]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_COUNT);

    rsb->global[id + EThread::STAT_LOOP_WAIT]->sum   = m->_wait;
    rsb->global[id + EThread::STAT_LOOP_WAIT]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_WAIT);

    rsb->global[id + EThread::STAT_LOOP_TIME_MIN]->sum   = m->_loop_time._min;
    rsb->global[id + EThread::STAT_LOOP_TIME_MIN]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_TIME_MIN);
    rsb->global[id + EThread::STAT_LOOP_TIME_MAX]->sum   = m->_loop_time._max;
    rsb->global[id + EThread::STAT_LOOP_TIME_MAX]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_TIME_MAX);

    rsb->global[id + EThread::STAT_LOOP_EVENTS]->sum   = m->_events._total;
    rsb->global[id + EThread::STAT_LOOP_EVENTS]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_EVENTS);
    rsb->global[id + EThread::STAT_LOOP_EVENTS_MIN]->sum   = m->_events._min;
    rsb->global[id + EThread::STAT_LOOP_EVENTS_MIN]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_EVENTS_MIN);
    rsb->global[id + EThread::STAT_LOOP_EVENTS_MAX]->sum   = m->_events._max;
    rsb->global[id + EThread::STAT_LOOP_EVENTS_MAX]->count = 1;
    RecRawStatUpdateSum(rsb, id + EThread::STAT_LOOP_EVENTS_MAX);
  }

  ink_mutex_release(&(rsb->mutex));
  return REC_ERR_OKAY;
}

/// This is a wrapper used to convert a static function into a continuation. The function pointer is
/// passed in the cookie. For this reason the class is used as a singleton.
/// @internal This is the implementation for @c schedule_spawn... overloads.
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
} // namespace

void *
ThreadAffinityInitializer::alloc_hugepage_stack(size_t stacksize)
{
  return ats_hugepage_enabled() ? ats_alloc_hugepage(stacksize) : ats_memalign(ats_pagesize(), stacksize);
}

#if TS_USE_HWLOC
void
ThreadAffinityInitializer::init()
{
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
    // fallthrough

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
}

int
ThreadAffinityInitializer::set_affinity(int, Event *)
{
  EThread *t = this_ethread();

  if (obj_count > 0) {
    // Get our `obj` instance with index based on the thread number we are on.
    hwloc_obj_t obj = hwloc_get_obj_by_type(ink_get_topology(), obj_type, t->id % obj_count);
#if HWLOC_API_VERSION >= 0x00010100
    int cpu_mask_len = hwloc_bitmap_snprintf(nullptr, 0, obj->cpuset) + 1;
    char *cpu_mask   = static_cast<char *>(alloca(cpu_mask_len));
    hwloc_bitmap_snprintf(cpu_mask, cpu_mask_len, obj->cpuset);
    Debug("iocore_thread", "EThread: %p %s: %d CPU Mask: %s\n", t, obj_name, obj->logical_index, cpu_mask);
#else
    Debug("iocore_thread", "EThread: %d %s: %d", _name, obj->logical_index);
#endif // HWLOC_API_VERSION
    hwloc_set_thread_cpubind(ink_get_topology(), t->tid, obj->cpuset, HWLOC_CPUBIND_STRICT);
  } else {
    Warning("hwloc returned an unexpected number of objects -- CPU affinity disabled");
  }
  return 0;
}

void *
ThreadAffinityInitializer::alloc_numa_stack(EThread *t, size_t stacksize)
{
  hwloc_membind_policy_t mem_policy = HWLOC_MEMBIND_DEFAULT;
  hwloc_nodeset_t nodeset           = hwloc_bitmap_alloc();
  int num_nodes                     = 0;
  void *stack                       = nullptr;
  hwloc_obj_t obj                   = hwloc_get_obj_by_type(ink_get_topology(), obj_type, t->id % obj_count);

  // Find the NUMA node set that correlates to our next thread CPU set
  hwloc_cpuset_to_nodeset(ink_get_topology(), obj->cpuset, nodeset);
  // How many NUMA nodes will we be needing to allocate across?
  num_nodes = hwloc_get_nbobjs_inside_cpuset_by_type(ink_get_topology(), obj->cpuset, HWLOC_OBJ_NODE);

  if (num_nodes == 1) {
    // The preferred memory policy. The thread lives in one NUMA node.
    mem_policy = HWLOC_MEMBIND_BIND;
  } else if (num_nodes > 1) {
    // If we have mode than one NUMA node we should interleave over them.
    mem_policy = HWLOC_MEMBIND_INTERLEAVE;
  }

  if (mem_policy != HWLOC_MEMBIND_DEFAULT) {
    // Let's temporarily set the memory binding to our destination NUMA node
#if HWLOC_API_VERSION >= 0x20000
    hwloc_set_membind(ink_get_topology(), nodeset, mem_policy, HWLOC_MEMBIND_THREAD | HWLOC_MEMBIND_BYNODESET);
#else
    hwloc_set_membind_nodeset(ink_get_topology(), nodeset, mem_policy, HWLOC_MEMBIND_THREAD);
#endif
  }

  // Alloc our stack
  stack = this->alloc_hugepage_stack(stacksize);

  if (mem_policy != HWLOC_MEMBIND_DEFAULT) {
    // Now let's set it back to default for this thread.
#if HWLOC_API_VERSION >= 0x20000
    hwloc_set_membind(ink_get_topology(), hwloc_topology_get_topology_nodeset(ink_get_topology()), HWLOC_MEMBIND_DEFAULT,
                      HWLOC_MEMBIND_THREAD | HWLOC_MEMBIND_BYNODESET);
#else
    hwloc_set_membind_nodeset(ink_get_topology(), hwloc_topology_get_topology_nodeset(ink_get_topology()), HWLOC_MEMBIND_DEFAULT,
                              HWLOC_MEMBIND_THREAD);
#endif
  }

  hwloc_bitmap_free(nodeset);

  return stack;
}

void *
ThreadAffinityInitializer::alloc_stack(EThread *t, size_t stacksize)
{
  return this->obj_count > 0 ? this->alloc_numa_stack(t, stacksize) : this->alloc_hugepage_stack(stacksize);
}

#else

void
ThreadAffinityInitializer::init()
{
}

int
ThreadAffinityInitializer::set_affinity(int, Event *)
{
  return 0;
}

void *
ThreadAffinityInitializer::alloc_stack(EThread *, size_t stacksize)
{
  return this->alloc_hugepage_stack(stacksize);
}

#endif // TS_USE_HWLOC

EventProcessor::EventProcessor() : thread_initializer(this)
{
  ink_zero(all_ethreads);
  ink_zero(all_dthreads);
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
} // namespace

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

  tg->_name = name;
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

  stacksize = std::max(stacksize, static_cast<decltype(stacksize)>(INK_THREAD_STACK_MIN));
  // Make sure it is a multiple of our page size
  if (ats_hugepage_enabled()) {
    stacksize = INK_ALIGN(stacksize, ats_hugepage_size());
  } else {
    stacksize = INK_ALIGN(stacksize, ats_pagesize());
  }

  Debug("iocore_thread", "Thread stack size set to %zu", stacksize);

  for (i = 0; i < n_threads; ++i) {
    EThread *t                   = new EThread(REGULAR, n_ethreads + i);
    all_ethreads[n_ethreads + i] = t;
    tg->_thread[i]               = t;
    t->id                        = i; // unfortunately needed to support affinity and NUMA logic.
    t->set_event_type(ev_type);
    t->schedule_spawn(&thread_initializer);
  }
  tg->_count = n_threads;
  n_ethreads += n_threads;
  schedule_spawn(&thread_started, ev_type);

  // Separate loop to avoid race conditions between spawn events and updating the thread table for
  // the group. Some thread set up depends on knowing the total number of threads but that can't be
  // safely updated until all the EThread instances are created and stored in the table.
  for (i = 0; i < n_threads; ++i) {
    Debug("iocore_thread_start", "Created %s thread #%d", tg->_name.c_str(), i + 1);
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[%s %d]", tg->_name.c_str(), i);
    void *stack = Thread_Affinity_Initializer.alloc_stack(tg->_thread[i], stacksize);
    tg->_thread[i]->start(thr_name, stack, stacksize);
  }

  Debug("iocore_thread", "Created thread group '%s' id %d with %d threads", tg->_name.c_str(), ev_type, n_threads);

  return ev_type; // useless but not sure what would be better.
}

// This is called from inside a thread as the @a start_event for that thread.  It chains to the
// startup events for the appropriate thread group start events.
void
EventProcessor::initThreadState(EThread *t)
{
  // Run all thread type initialization continuations that match the event types for this thread.
  for (int i = 0; i < MAX_EVENT_TYPES; ++i) {
    if (t->is_event_type(i)) {
      // To avoid race conditions on the event in the spawn queue, create a local one to actually send.
      // Use the spawn queue event as a read only model.
      Event *nev = eventAllocator.alloc();
      for (Event *ev = thread_group[i]._spawnQueue.head; nullptr != ev; ev = ev->link.next) {
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

  // Get our statistics set up
  RecRawStatBlock *rsb = RecAllocateRawStatBlock(EThread::N_EVENT_STATS * EThread::N_EVENT_TIMESCALES);
  char name[256];

  for (int ts_idx = 0; ts_idx < EThread::N_EVENT_TIMESCALES; ++ts_idx) {
    for (int id = 0; id < EThread::N_EVENT_STATS; ++id) {
      snprintf(name, sizeof(name), "%s.%ds", EThread::STAT_NAME[id], EThread::SAMPLE_COUNT[ts_idx]);
      RecRegisterRawStat(rsb, RECT_PROCESS, name, RECD_INT, RECP_NON_PERSISTENT, id + (ts_idx * EThread::N_EVENT_STATS), NULL);
    }
  }

  // Name must be that of a stat, pick one at random since we do all of them in one pass/callback.
  RecRegisterRawStatSyncCb(name, EventMetricStatSync, rsb, 0);

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

bool
EventProcessor::has_tg_started(int etype)
{
  return thread_group[etype]._started == thread_group[etype]._count;
}

void
thread_started(EThread *t)
{
  // Find what type of thread this is, and increment the "_started" counter of that thread type.
  for (int i = 0; i < MAX_EVENT_TYPES; ++i) {
    if (t->is_event_type(i)) {
      if (++eventProcessor.thread_group[i]._started == eventProcessor.thread_group[i]._count &&
          eventProcessor.thread_group[i]._afterStartCallback != nullptr) {
        eventProcessor.thread_group[i]._afterStartCallback();
      }
      break;
    }
  }
}
