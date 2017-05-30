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

EventType
EventProcessor::spawn_event_threads(int n_threads, const char *et_name, size_t stacksize)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  EventType new_thread_group_id;
  int i;

  ink_release_assert(n_threads > 0);
  ink_release_assert((n_ethreads + n_threads) <= MAX_EVENT_THREADS);
  ink_release_assert(n_thread_groups < MAX_EVENT_TYPES);

  new_thread_group_id = (EventType)n_thread_groups;

  for (i = 0; i < n_threads; i++) {
    EThread *t                          = new EThread(REGULAR, n_ethreads + i);
    all_ethreads[n_ethreads + i]        = t;
    eventthread[new_thread_group_id][i] = t;
    t->set_event_type(new_thread_group_id);
  }

  n_threads_for_type[new_thread_group_id] = n_threads;
  for (i = 0; i < n_threads; i++) {
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[%s %d]", et_name, i);
    eventthread[new_thread_group_id][i]->start(thr_name, stacksize, nullptr, nullptr, nullptr);
  }

  n_thread_groups++;
  n_ethreads += n_threads;
  Debug("iocore_thread", "Created thread group '%s' id %d with %d threads", et_name, new_thread_group_id, n_threads);

  return new_thread_group_id;
}

static void *
alloc_stack(size_t stacksize)
{
  void *stack = nullptr;

  if (ats_hugepage_enabled()) {
    stack = ats_alloc_hugepage(stacksize);
  }

  if (stack == nullptr) {
    stack = ats_memalign(ats_pagesize(), stacksize);
  }

  return stack;
}

#if TS_USE_HWLOC
static void *
alloc_numa_stack(hwloc_cpuset_t cpuset, size_t stacksize)
{
  hwloc_membind_policy_t mem_policy = HWLOC_MEMBIND_DEFAULT;
  hwloc_nodeset_t nodeset           = hwloc_bitmap_alloc();
  int num_nodes                     = 0;
  void *stack                       = nullptr;

  // Find the NUMA node set that correlates to our next thread CPU set
  hwloc_cpuset_to_nodeset(ink_get_topology(), cpuset, nodeset);
  // How many NUMA nodes will we be needing to allocate across?
  num_nodes = hwloc_get_nbobjs_inside_cpuset_by_type(ink_get_topology(), cpuset, HWLOC_OBJ_NODE);

  if (num_nodes == 1) {
    // The preferred memory policy. The thread lives in one NUMA node.
    mem_policy = HWLOC_MEMBIND_BIND;
  } else if (num_nodes > 1) {
    // If we have mode than one NUMA node we should interleave over them.
    mem_policy = HWLOC_MEMBIND_INTERLEAVE;
  }

  if (mem_policy != HWLOC_MEMBIND_DEFAULT) {
    // Let's temporarily set the memory binding to our destination NUMA node
    hwloc_set_membind_nodeset(ink_get_topology(), nodeset, mem_policy, HWLOC_MEMBIND_THREAD);
  }

  // Alloc our stack
  stack = alloc_stack(stacksize);

  if (mem_policy != HWLOC_MEMBIND_DEFAULT) {
    // Now let's set it back to default for this thread.
    hwloc_set_membind_nodeset(ink_get_topology(), hwloc_topology_get_topology_nodeset(ink_get_topology()), HWLOC_MEMBIND_DEFAULT,
                              HWLOC_MEMBIND_THREAD);
  }

  hwloc_bitmap_free(nodeset);

  return stack;
}
#endif // TS_USE_HWLOC

class EventProcessor eventProcessor;

int
EventProcessor::start(int n_event_threads, size_t stacksize)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  int i;
  void *stack = nullptr;

  // do some sanity checking.
  static int started = 0;
  ink_release_assert(!started);
  ink_release_assert(n_event_threads > 0 && n_event_threads <= MAX_EVENT_THREADS);
  started = 1;

  n_ethreads      = n_event_threads;
  n_thread_groups = 1;

  // Make sure that our thread stack size is at least the minimum size
  stacksize = MAX(stacksize, INK_THREAD_STACK_MIN);

  // Make sure it is a multiple of our page size
  if (ats_hugepage_enabled()) {
    stacksize = INK_ALIGN(stacksize, ats_hugepage_size());
  } else {
    stacksize = INK_ALIGN(stacksize, ats_pagesize());
  }

  Debug("iocore_thread", "Thread stack size set to %zu", stacksize);

  for (i = 0; i < n_event_threads; i++) {
    EThread *t      = new EThread(REGULAR, i);
    all_ethreads[i] = t;

    eventthread[ET_CALL][i] = t;
    t->set_event_type((EventType)ET_CALL);
  }
  n_threads_for_type[ET_CALL] = n_event_threads;

#if TS_USE_HWLOC
  int affinity = 1;
  REC_ReadConfigInteger(affinity, "proxy.config.exec_thread.affinity");
  hwloc_obj_t obj;
  hwloc_obj_type_t obj_type;
  int obj_count = 0;
  char *obj_name;

  switch (affinity) {
  case 4: // assign threads to logical processing units
// Older versions of libhwloc (eg. Ubuntu 10.04) don't have HWLOC_OBJ_PU.
#if HAVE_HWLOC_OBJ_PU
    obj_type = HWLOC_OBJ_PU;
    obj_name = (char *)"Logical Processor";
    break;
#endif
  case 3: // assign threads to real cores
    obj_type = HWLOC_OBJ_CORE;
    obj_name = (char *)"Core";
    break;
  case 1: // assign threads to NUMA nodes (often 1:1 with sockets)
    obj_type = HWLOC_OBJ_NODE;
    obj_name = (char *)"NUMA Node";
    if (hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type) > 0) {
      break;
    }
  case 2: // assign threads to sockets
    obj_type = HWLOC_OBJ_SOCKET;
    obj_name = (char *)"Socket";
    break;
  default: // assign threads to the machine as a whole (a level below SYSTEM)
    obj_type = HWLOC_OBJ_MACHINE;
    obj_name = (char *)"Machine";
  }

  // How many of the above `obj_type` do we have in our topology?
  obj_count = hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type);
  Debug("iocore_thread", "Affinity: %d %ss: %d PU: %d", affinity, obj_name, obj_count, ink_number_of_processors());

#endif
  for (i = 0; i < n_ethreads; i++) {
    ink_thread tid;

#if TS_USE_HWLOC
    if (obj_count > 0) {
      // Get our `obj` instance with index based on the thread number we are on.
      obj = hwloc_get_obj_by_type(ink_get_topology(), obj_type, i % obj_count);
#if HWLOC_API_VERSION >= 0x00010100
      // Pretty print our CPU set
      int cpu_mask_len = hwloc_bitmap_snprintf(nullptr, 0, obj->cpuset) + 1;
      char *cpu_mask   = (char *)alloca(cpu_mask_len);
      hwloc_bitmap_snprintf(cpu_mask, cpu_mask_len, obj->cpuset);
      Debug("iocore_thread", "EThread: %d %s: %d CPU Mask: %s", i, obj_name, obj->logical_index, cpu_mask);
#else
      Debug("iocore_thread", "EThread: %d %s: %d", i, obj_name, obj->logical_index);
#endif // HWLOC_API_VERSION
    }
#endif // TS_USE_HWLOC

    // Name our thread
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ET_NET %d]", i);
#if TS_USE_HWLOC
    // Lets create a NUMA local stack if we can
    if (obj_count > 0) {
      stack = alloc_numa_stack(obj->cpuset, stacksize);
    } else {
      // Lets just alloc a stack even with no NUMA knowledge
      stack = alloc_stack(stacksize);
    }
#else
    // Lets just alloc a stack even with no NUMA knowledge
    stack = alloc_stack(stacksize);
#endif // TS_USE_HWLOC

    // Start our new thread with our new stack.
    tid   = all_ethreads[i]->start(thr_name, stacksize, nullptr, nullptr, stack);
    stack = nullptr;

#if TS_USE_HWLOC
    if (obj_count > 0) {
      // Lets bind our new thread to it's CPU set
      hwloc_set_thread_cpubind(ink_get_topology(), tid, obj->cpuset, HWLOC_CPUBIND_STRICT);
    } else {
      Warning("hwloc returned an unexpected value -- CPU affinity disabled");
    }
#else
    // Lets ignore tid if we don't link with HWLOC
    (void)tid;
#endif // TS_USE_HWLOC
  }

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
     as the same entry is overwritten while another is left unitialized.
     The other is read/write contention where another thread (e.g. the stats collection thread) is
     iterating over the threads while the active count (@a n_dthreads) is being updated causing use
     of not yet initialized array element.
     This logic covers both situations. For write/write the actual array update is locked. The
     potentially expensive set up is done outside the lock making the time spent locked small For
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
    ink_mutex_acquire(&dedicated_spawn_thread_mutex);
    ink_release_assert(n_dthreads < MAX_EVENT_THREADS);
    all_dthreads[n_dthreads] = e->ethread;
    ++n_dthreads; // Be very sure this is after the array element update.
    ink_mutex_release(&dedicated_spawn_thread_mutex);
  }

  e->ethread->start(thr_name, stacksize, nullptr, nullptr, nullptr);

  return e;
}
