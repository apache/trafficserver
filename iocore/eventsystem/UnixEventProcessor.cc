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

#if HAVE_SYS_PSET_H
#include <sys/pset.h>
typedef psetid_t ink_cpuset_t;
#define PTR_FMT "u"
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
bind_cpu(ink_cpuset_t *cpuset, ink_thread tid)
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
EventProcessor::spawn_event_threads(int n_threads, const char* et_name, size_t stacksize)
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
    eventthread[new_thread_group_id][i]->start(thr_name, stacksize);
  }

  n_thread_groups++;
  n_ethreads += n_threads;
  Debug("iocore_thread", "Created thread group '%s' id %d with %d threads", et_name, new_thread_group_id, n_threads); 

  return new_thread_group_id;
}


class EventProcessor eventProcessor;

int
EventProcessor::start(int n_event_threads, size_t stacksize)
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
  // Commenting this out as a fix for 4.2.x only. This reverts the fix
  // that exposed another bug and returns to the behavior previous to 4.2.x.
  // This is also now fixed in master.
  // REC_ReadConfigInteger(affinity, "proxy.config.exec_thread.affinity");
  ink_cpuset_t cpuset;
  int socket = hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_SOCKET);
  int cu = hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_CORE);
  int pu = cu;

  // Older versions of libhwloc (eg. Ubuntu 10.04) don't have pHWLOC_OBJ_PU.
#if HAVE_HWLOC_OBJ_PU
  pu = hwloc_get_nbobjs_by_type(ink_get_topology(), HWLOC_OBJ_PU);
#endif

  Debug("iocore_thread", "socket: %d core: %d logical processor: %d affinity: %d", socket, cu, pu, affinity);
#endif

  for (i = first_thread; i < n_ethreads; i++) {
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ET_NET %d]", i);
    ink_thread tid = all_ethreads[i]->start(thr_name, stacksize);
    (void)tid;

#if TS_USE_HWLOC
    if (affinity != 0) {
      int logical_ratio;
      switch(affinity) {
      case 3:           // assign threads to logical cores
        logical_ratio = 1;
        break;
      case 2:           // assign threads to real cores
        logical_ratio = pu / cu;
        break;
      case 1:           // assign threads to sockets
      default:
        logical_ratio = pu / socket;
      }

      char debug_message[256];
      int len = snprintf(debug_message, sizeof(debug_message), "setaffinity tid: %" PTR_FMT ", net thread: %u cpu:", tid, i);
      for (int cpu_count = 0; cpu_count < logical_ratio; cpu_count++) {
        int cpu = ((i - 1) * logical_ratio + cpu_count) % pu;
        set_cpu(&cpuset, cpu);
        len += snprintf(debug_message + len, sizeof(debug_message) - len, " %d", cpu);
      }
      Debug("iocore_thread", "%s", debug_message);
      if (!bind_cpu(&cpuset, tid)){
        Error("%s, failed with errno: %d", debug_message, errno);
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
EventProcessor::spawn_thread(Continuation *cont, const char* thr_name, size_t stacksize, ink_sem *sem)
{
  ink_release_assert(n_dthreads < MAX_EVENT_THREADS);
  Event *e = eventAllocator.alloc();

  e->init(cont, 0, 0);
  all_dthreads[n_dthreads] = NEW(new EThread(DEDICATED, e, sem));
  e->ethread = all_dthreads[n_dthreads];
  e->mutex = e->continuation->mutex = all_dthreads[n_dthreads]->mutex;
  n_dthreads++;
  e->ethread->start(thr_name, stacksize);

  return e;
}
