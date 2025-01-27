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

/**************************************************************************
  Generic threads interface

**************************************************************************/

#pragma once

#include "tscore/ink_hrtime.h"
#include "tscore/ink_defs.h"
#include <sched.h>
#if TS_USE_HWLOC
#include "tscore/ink_hw.h"
#if USE_NUMA
#include <hwloc/glibc-sched.h>
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
//      The POSIX threads interface
//
//////////////////////////////////////////////////////////////////////////////

#include <pthread.h>
#include <csignal>
#include <semaphore.h>

#if __has_include(<pthread_np.h>)
#include <pthread_np.h>
#endif

#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#endif

#define INK_MUTEX_INIT       PTHREAD_MUTEX_INITIALIZER
#define INK_THREAD_STACK_MIN PTHREAD_STACK_MIN

using ink_thread     = pthread_t;
using ink_cond       = pthread_cond_t;
using ink_thread_key = pthread_key_t;

// Darwin has a sem_init stub that returns ENOSYS. Rather than bodge around doing runtime
// detection, just emulate on Darwin.
#if defined(darwin)
#define TS_EMULATE_ANON_SEMAPHORES 1
#endif

struct ink_semaphore {
#if TS_EMULATE_ANON_SEMAPHORES
  sem_t  *sema;
  int64_t semid;
#else
  sem_t sema;
#endif

  sem_t *
  get()
  {
#if TS_EMULATE_ANON_SEMAPHORES
    return sema;
#else
    return &sema;
#endif
  }
};

/*******************************************************************
 *** Condition variables
 ******************************************************************/

using ink_timestruc = struct timespec;

#include <cerrno>
#include "tscore/ink_mutex.h"
#include "tscore/ink_assert.h"

//////////////////////////////////////////////////////////////////////////////
//
//      The POSIX threads interface
//
//////////////////////////////////////////////////////////////////////////////

// NOTE(cmcfarlen): removed posix thread local key functions, use thread_local

static inline void
ink_thread_create(ink_thread *tid, void *(*f)(void *), void *a, int detached, size_t stacksize, void *stack
#if TS_USE_HWLOC && TS_USE_NUMA
                  ,
                  hwloc_cpuset_t cpuset = nullptr
#endif
)
{
  ink_thread     t;
  int            ret;
  pthread_attr_t attr;

  if (tid == nullptr) {
    tid = &t;
  }

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  if (stacksize) {
    if (stack) {
      pthread_attr_setstack(&attr, stack, stacksize);
    } else {
      pthread_attr_setstacksize(&attr, stacksize);
    }
  }

#if TS_USE_HWLOC && TS_USE_NUMA
  if (cpuset) {
    size_t     schedset_sz = CPU_ALLOC_SIZE(ink_number_of_processors());
    cpu_set_t *schedset    = (cpu_set_t *)alloca(schedset_sz);
    int        err         = hwloc_cpuset_to_glibc_sched_affinity(ink_get_topology(), cpuset, schedset, schedset_sz);
    if (err != 0) {
      ink_abort("hwloc_cpuset_to_glibc_sched_affinity failed: %d", err);
    }
    err = pthread_attr_setaffinity_np(&attr, schedset_sz, schedset);
    if (err != 0) {
      ink_abort("pthread_attr_setaffinity_np failed: %d", err);
    }
  }
#endif

  if (detached) {
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  }

  ret = pthread_create(tid, &attr, f, a);
  if (ret != 0) {
    ink_abort("pthread_create() failed: %s (%d)", strerror(ret), ret);
  }
  pthread_attr_destroy(&attr);
}

static inline void
ink_thread_cancel(ink_thread who)
{
  int ret = pthread_cancel(who);
  ink_assert(ret == 0);
}

static inline void *
ink_thread_join(ink_thread t)
{
  void *r;
  ink_assert(!pthread_join(t, &r));
  return r;
}

static inline ink_thread
ink_thread_self()
{
  return (pthread_self());
}

static inline ink_thread
ink_thread_null()
{
  // The implementation of ink_thread (the alias of pthread_t) is different on platforms
  // - e.g. `struct pthread *` on Unix and `unsigned long int` on Linux
  // NOLINTNEXTLINE(modernize-use-nullptr)
  return static_cast<ink_thread>(0);
}

static inline int
ink_thread_get_priority(ink_thread t, int *priority)
{
  int                policy;
  struct sched_param param;
  int                res = pthread_getschedparam(t, &policy, &param);
  *priority              = param.sched_priority;
  return res;
}

static inline int
ink_thread_sigsetmask(int how, const sigset_t *set, sigset_t *oset)
{
  return (pthread_sigmask(how, set, oset));
}

static inline int
ink_thread_kill(ink_thread t, int sig)
{
  return pthread_kill(t, sig);
}

/*******************************************************************
 * Posix Semaphores
 ******************************************************************/

void ink_sem_init(ink_semaphore *sp, unsigned int count);
void ink_sem_destroy(ink_semaphore *sp);
void ink_sem_wait(ink_semaphore *sp);
bool ink_sem_trywait(ink_semaphore *sp);
void ink_sem_post(ink_semaphore *sp);

/*******************************************************************
 * Posix Condition Variables
 ******************************************************************/

static inline void
ink_cond_init(ink_cond *cp)
{
  ink_assert(pthread_cond_init(cp, nullptr) == 0);
}

static inline void
ink_cond_destroy(ink_cond *cp)
{
  ink_assert(pthread_cond_destroy(cp) == 0);
}

static inline void
ink_cond_wait(ink_cond *cp, ink_mutex *mp)
{
  ink_assert(pthread_cond_wait(cp, mp) == 0);
}

static inline int
ink_cond_timedwait(ink_cond *cp, ink_mutex *mp, ink_timestruc *t)
{
  int err;
  while (EINTR == (err = pthread_cond_timedwait(cp, mp, t))) {
    ;
  }
#ifndef ETIME
// ink_defs.h aliases ETIME to ETIMEDOUT and this path should never happen
#warning Unknown ETIME return condition for pthread_cond_timedwait
  ink_assert((err == 0) || (err == ETIMEDOUT));
#else
  ink_assert((err == 0) || (err == ETIME) || (err == ETIMEDOUT));
#endif
  return err;
}

static inline void
ink_cond_signal(ink_cond *cp)
{
  ink_assert(pthread_cond_signal(cp) == 0);
}

static inline void
ink_cond_broadcast(ink_cond *cp)
{
  ink_assert(pthread_cond_broadcast(cp) == 0);
}

static inline void
ink_thr_yield()
{
  ink_assert(!sched_yield());
}

static inline void
ink_thread_exit(void *status)
{
  pthread_exit(status);
}

// This define is from Linux's <sys/prctl.h> and is most likely very
// Linux specific... Feel free to add support for other platforms
// that has a feature to give a thread specific name / tag.
static inline void
ink_set_thread_name(const char *name ATS_UNUSED)
{
#if defined(HAVE_PTHREAD_SETNAME_NP_1)
  pthread_setname_np(name);
#elif defined(HAVE_PTHREAD_SETNAME_NP_2)
  pthread_setname_np(pthread_self(), name);
#elif defined(HAVE_PTHREAD_SET_NAME_NP_2)
  pthread_set_name_np(pthread_self(), name);
#elif HAVE_PRCTL && defined(PR_SET_NAME)
  prctl(PR_SET_NAME, name, 0, 0, 0);
#endif
}

static inline void
ink_get_thread_name(char *name, size_t len)
{
#if defined(HAVE_PTHREAD_GETNAME_NP)
  pthread_getname_np(pthread_self(), name, len);
#elif defined(HAVE_PTHREAD_GET_NAME_NP)
  pthread_get_name_np(pthread_self(), name, len);
#elif HAVE_PRCTL && defined(PR_GET_NAME)
  prctl(PR_GET_NAME, name, 0, 0, 0);
#else
  snprintf(name, len, "0x%" PRIx64, (uint64_t)ink_thread_self());
#endif
}
