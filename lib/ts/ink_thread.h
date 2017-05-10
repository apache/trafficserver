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

#ifndef _INK_THREAD_H
#define _INK_THREAD_H

#include "ts/ink_hrtime.h"
#include "ts/ink_defs.h"
#include <sched.h>

//////////////////////////////////////////////////////////////////////////////
//
//      The POSIX threads interface
//
//////////////////////////////////////////////////////////////////////////////

#if defined(POSIX_THREAD)
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>

#if HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif

#define INK_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define INK_THREAD_STACK_MIN PTHREAD_STACK_MIN

typedef pthread_t ink_thread;
typedef pthread_cond_t ink_cond;
typedef pthread_key_t ink_thread_key;

// Darwin has a sem_init stub that returns ENOSYS. Rather than bodge around doing runtime
// detection, just emulate on Darwin.
#if defined(darwin)
#define TS_EMULATE_ANON_SEMAPHORES 1
#endif

struct ink_semaphore {
#if TS_EMULATE_ANON_SEMAPHORES
  sem_t *sema;
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

#endif /* #if defined(POSIX_THREAD) */

/*******************************************************************
 *** Condition variables
 ******************************************************************/

#ifdef POSIX_THREAD_10031c
typedef struct timespec ink_timestruc;
#else
typedef timestruc_t ink_timestruc;
#endif

#include <errno.h>
#include "ts/ink_mutex.h"
#include "ts/ink_assert.h"

//////////////////////////////////////////////////////////////////////////////
//
//      The POSIX threads interface
//
//////////////////////////////////////////////////////////////////////////////
#if defined(POSIX_THREAD)

static inline void
ink_thread_key_create(ink_thread_key *key, void (*destructor)(void *value))
{
  ink_assert(!pthread_key_create(key, destructor));
}

static inline void
ink_thread_setspecific(ink_thread_key key, void *value)
{
  ink_assert(!pthread_setspecific(key, value));
}

static inline void *
ink_thread_getspecific(ink_thread_key key)
{
  return pthread_getspecific(key);
}

static inline void
ink_thread_key_delete(ink_thread_key key)
{
  ink_assert(!pthread_key_delete(key));
}

static inline ink_thread
ink_thread_create(void *(*f)(void *), void *a, int detached, size_t stacksize, void *stack)
{
  ink_thread t;
  int ret;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  if (stacksize) {
    if (stack) {
      pthread_attr_setstack(&attr, stack, stacksize);
    } else {
      pthread_attr_setstacksize(&attr, stacksize);
    }
  }

  if (detached) {
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  }

  ret = pthread_create(&t, &attr, f, a);
  if (ret != 0) {
    ink_abort("pthread_create() failed: %s (%d)", strerror(ret), ret);
  }
  pthread_attr_destroy(&attr);

  return t;
}

static inline void
ink_thread_cancel(ink_thread who)
{
#if defined(freebsd)
  (void)who;
  ink_assert(!"not supported");
#else
  int ret = pthread_cancel(who);
  ink_assert(ret == 0);
#endif
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
  return (ink_thread)0;
}

static inline int
ink_thread_get_priority(ink_thread t, int *priority)
{
#if defined(freebsd)
  (void)t;
  (void)priority;
  ink_assert(!"not supported");
  return -1;
#else
  int policy;
  struct sched_param param;
  int res   = pthread_getschedparam(t, &policy, &param);
  *priority = param.sched_priority;
  return res;
#endif
}

static inline int
ink_thread_sigsetmask(int how, const sigset_t *set, sigset_t *oset)
{
  return (pthread_sigmask(how, set, oset));
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
  while (EINTR == (err = pthread_cond_timedwait(cp, mp, t)))
    ;
#if defined(freebsd) || defined(openbsd)
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
#elif defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_NAME)
  prctl(PR_SET_NAME, name, 0, 0, 0);
#endif
}

#endif /* #if defined(POSIX_THREAD) */

#endif /*_INK_THREAD_H*/
