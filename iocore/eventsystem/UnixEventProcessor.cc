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

#if HAVE_HWLOC_H
#include <hwloc.h>
#endif

#if HAVE_SCHED_H
#include <sched.h>
#if !defined(solaris) && !defined(freebsd)
typedef cpu_set_t ink_cpuset_t;
#define PTR_FMT PRIuPTR
#endif
#endif

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if HAVE_SYS_CPUSET_H
#include <sys/cpuset.h>
typedef cpuset_t ink_cpuset_t;
#define PTR_FMT "p"
#endif

#if HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif

#if HAVE_SYS_PSET_H
#include <sys/pset.h>
typedef psetid_t ink_cpuset_t;
#define PTR_FMT PRIuPTR
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#endif
#include "ink_defs.h"

#if TS_USE_HWLOC
static void
set_cpu(ink_cpuset_t *cpuset, int cpu)
{
#if !defined(solaris)
  CPU_ZERO(cpuset);
  CPU_SET(cpu, cpuset);
#else
  pset_create(cpuset);
  pset_assign(*cpuset, cpu, NULL);
#endif
}


static bool
bind_cpu(ink_cpuset_t *cpuset, pthread_t tid)
{
  if ( 0 != 
#if !defined(solaris)
    pthread_setaffinity_np(tid, sizeof(ink_cpuset_t), cpuset)
#else
    pset_bind(*cpuset, P_LWPID, P_MYID, NULL)
#endif
    ){
    return false;
  }
  return true;
}
#endif

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
  ink_cpuset_t cpuset;
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
      int cpu = (i - 1) % num_cpus;
      set_cpu(&cpuset, cpu);
      Debug("iocore_thread", "setaffinity tid: %" PTR_FMT ", net thread: %u, cpu: %d", tid, i, cpu);
      if (!bind_cpu(&cpuset, tid)){
        Debug("iocore_thread", "setaffinity for tid: %" PTR_FMT ", net thread: %u, cpu: %d failed with: %d", tid, i, cpu, errno);
      }
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
