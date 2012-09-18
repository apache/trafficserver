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

#include "P_EventSystem.h"      /* MAGIC_EDITING_TAG */
#include <sched.h>
#if TS_USE_HWLOC
#include <hwloc.h>
// TODO: (humbedooh): Make this build on Solaris with hwloc.
#if defined(freebsd) || defined(solaris)
#include <sys/cpuset.h>
#define cpu_set_t cpuset_t
#include <pthread_np.h>
#endif
#endif
#include "ink_defs.h"

EventType
EventProcessor::spawn_event_threads(int n_threads, const char* et_name)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  EventType new_thread_group_id;
  int i;

  ink_release_assert(n_threads > 0);
  ink_release_assert((n_ethreads + n_threads) <= MAX_EVENT_THREADS);
  ink_release_assert(n_thread_groups < MAX_EVENT_TYPES);

  new_thread_group_id = (EventType) n_thread_groups;

  for (i = 0; i < n_threads; i++) {
    EThread *t = NEW(new EThread(REGULAR, n_ethreads + i));
    all_ethreads[n_ethreads + i] = t;
    eventthread[new_thread_group_id][i] = t;
    t->set_event_type(new_thread_group_id);
  }

  n_threads_for_type[new_thread_group_id] = n_threads;
  for (i = 0; i < n_threads; i++) {
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[%s %d]", et_name, i);
    eventthread[new_thread_group_id][i]->start(thr_name);
  }

  n_thread_groups++;
  n_ethreads += n_threads;
  Debug("iocore_thread", "Created thread group '%s' id %d with %d threads", et_name, new_thread_group_id, n_threads); 

  return new_thread_group_id;
}


#define INK_NO_CLUSTER

class EventProcessor eventProcessor;

int
EventProcessor::start(int n_event_threads)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  int i;

  // do some sanity checking.
  static int started = 0;
  ink_release_assert(!started);
  ink_release_assert(n_event_threads > 0 && n_event_threads <= MAX_EVENT_THREADS);
  started = 1;

  n_ethreads = n_event_threads;
  n_thread_groups = 1;

  int first_thread = 1;

  for (i = 0; i < n_event_threads; i++) {
    EThread *t = NEW(new EThread(REGULAR, i));
    if (first_thread && !i) {
      ink_thread_setspecific(Thread::thread_data_key, t);
      global_mutex = t->mutex;
      t->cur_time = ink_get_based_hrtime_internal();
    }
    all_ethreads[i] = t;

    eventthread[ET_CALL][i] = t;
    t->set_event_type((EventType) ET_CALL);
  }
  n_threads_for_type[ET_CALL] = n_event_threads;

#if TS_USE_HWLOC
  int affinity = 0;
  REC_ReadConfigInteger(affinity, "proxy.config.exec_thread.affinity");
  cpu_set_t cpuset;
  const hwloc_topology_t *topology = ink_get_topology();
  int cu = hwloc_get_nbobjs_by_type(*topology, HWLOC_OBJ_CORE);
  int pu = hwloc_get_nbobjs_by_type(*topology, HWLOC_OBJ_PU);
  int num_cpus = cu;
  Debug("iocore_thread", "cu: %d pu: %d affinity: %d", cu, pu, affinity);
#endif

  for (i = first_thread; i < n_ethreads; i++) {
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ET_NET %d]", i);
    ink_thread tid = all_ethreads[i]->start(thr_name);
    (void)tid;

#if TS_USE_HWLOC
    if (affinity == 1) {
      CPU_ZERO(&cpuset);
      int cpu = (i - 1) % num_cpus;
      CPU_SET(cpu, &cpuset);
      Debug("iocore_thread", "setaffinity tid: %p, net thread: %d, cpu: %d", tid, i, cpu);
      assert(pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset) == 0);
    }
#endif
  }
  Debug("iocore_thread", "Created event thread group id %d with %d threads", ET_CALL, n_event_threads);
  return 0;
}

void
EventProcessor::shutdown()
{
}

Event *
EventProcessor::spawn_thread(Continuation *cont, const char* thr_name, ink_sem *sem)
{
  ink_release_assert(n_dthreads < MAX_EVENT_THREADS);
  Event *e = eventAllocator.alloc();

  e->init(cont, 0, 0);
  dthreads[n_dthreads] = NEW(new EThread(DEDICATED, e, sem));
  e->ethread = dthreads[n_dthreads];
  e->mutex = e->continuation->mutex = dthreads[n_dthreads]->mutex;
  n_dthreads++;
  e->ethread->start(thr_name);

  return e;
}
