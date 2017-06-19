/** @file

  Basic locks for threads

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

#ifndef _I_Lock_h_
#define _I_Lock_h_

#include "ts/ink_platform.h"
#include "ts/Diags.h"
#include "I_Thread.h"

#define MAX_LOCK_TIME HRTIME_MSECONDS(200)
#define THREAD_MUTEX_THREAD_HOLDING (-1024 * 1024)

/*------------------------------------------------------*\
|  Macros                                                |
\*------------------------------------------------------*/

/**
  Blocks until the lock to the ProxyMutex is acquired.

  This macro performs a blocking call until the lock to the ProxyMutex
  is acquired. This call allocates a special object that holds the
  lock to the ProxyMutex only for the scope of the function or
  region. It is a good practice to delimit such scope explicitly
  with '&#123;' and '&#125;'.

  @param _l Arbitrary name for the lock to use in this call
  @param _m A pointer to (or address of) a ProxyMutex object
  @param _t The current EThread executing your code.

*/
#ifdef DEBUG
#define SCOPED_MUTEX_LOCK(_l, _m, _t) MutexLock _l(MakeSourceLocation(), nullptr, _m, _t)
#else
#define SCOPED_MUTEX_LOCK(_l, _m, _t) MutexLock _l(_m, _t)
#endif // DEBUG

#ifdef DEBUG
/**
  Attempts to acquire the lock to the ProxyMutex.

  This macro attempts to acquire the lock to the specified ProxyMutex
  object in a non-blocking manner. After using the macro you can
  see if it was successful by comparing the lock variable with true
  or false (the variable name passed in the _l parameter).

  @param _l Arbitrary name for the lock to use in this call (lock variable)
  @param _m A pointer to (or address of) a ProxyMutex object
  @param _t The current EThread executing your code.

*/
#define MUTEX_TRY_LOCK(_l, _m, _t) MutexTryLock _l(MakeSourceLocation(), (char *)nullptr, _m, _t)

/**
  Attempts to acquire the lock to the ProxyMutex.

  This macro performs up to the specified number of attempts to
  acquire the lock on the ProxyMutex object. It does so by running
  a busy loop (busy wait) '_sc' times. You should use it with care
  since it blocks the thread during that time and wastes CPU time.

  @param _l Arbitrary name for the lock to use in this call (lock variable)
  @param _m A pointer to (or address of) a ProxyMutex object
  @param _t The current EThread executing your code.
  @param _sc The number of attempts or spin count. It must be a positive value.

*/
#define MUTEX_TRY_LOCK_SPIN(_l, _m, _t, _sc) MutexTryLock _l(MakeSourceLocation(), (char *)nullptr, _m, _t, _sc)

/**
  Attempts to acquire the lock to the ProxyMutex.

  This macro attempts to acquire the lock to the specified ProxyMutex
  object in a non-blocking manner. After using the macro you can
  see if it was successful by comparing the lock variable with true
  or false (the variable name passed in the _l parameter).

  @param _l Arbitrary name for the lock to use in this call (lock variable)
  @param _m A pointer to (or address of) a ProxyMutex object
  @param _t The current EThread executing your code.
  @param _c Continuation whose mutex will be attempted to lock.

*/

#define MUTEX_TRY_LOCK_FOR(_l, _m, _t, _c) MutexTryLock _l(MakeSourceLocation(), nullptr, _m, _t)
#else // DEBUG
#define MUTEX_TRY_LOCK(_l, _m, _t) MutexTryLock _l(_m, _t)
#define MUTEX_TRY_LOCK_SPIN(_l, _m, _t, _sc) MutexTryLock _l(_m, _t, _sc)
#define MUTEX_TRY_LOCK_FOR(_l, _m, _t, _c) MutexTryLock _l(_m, _t)
#endif // DEBUG

/**
  Releases the lock on a ProxyMutex.

  This macro releases the lock on the ProxyMutex, provided it is
  currently held. The lock must have been successfully acquired
  with one of the MUTEX macros.

  @param _l Arbitrary name for the lock to use in this call (lock
    variable) It must be the same name as the one used to acquire the
    lock.

*/
#define MUTEX_RELEASE(_l) (_l).release()

/////////////////////////////////////
// DEPRECATED DEPRECATED DEPRECATED
#ifdef DEBUG
#define MUTEX_TAKE_TRY_LOCK(_m, _t) Mutex_trylock(MakeSourceLocation(), (char *)nullptr, _m, _t)
#define MUTEX_TAKE_TRY_LOCK_FOR(_m, _t, _c) Mutex_trylock(MakeSourceLocation(), (char *)nullptr, _m, _t)
#define MUTEX_TAKE_TRY_LOCK_FOR_SPIN(_m, _t, _c, _sc) Mutex_trylock_spin(MakeSourceLocation(), nullptr, _m, _t, _sc)
#else
#define MUTEX_TAKE_TRY_LOCK(_m, _t) Mutex_trylock(_m, _t)
#define MUTEX_TAKE_TRY_LOCK_FOR(_m, _t, _c) Mutex_trylock(_m, _t)
#define MUTEX_TAKE_TRY_LOCK_FOR_SPIN(_m, _t, _c, _sc) Mutex_trylock_spin(_m, _t, _sc)
#endif

#ifdef DEBUG
#define MUTEX_TAKE_LOCK(_m, _t) Mutex_lock(MakeSourceLocation(), (char *)nullptr, _m, _t)
#define MUTEX_TAKE_LOCK_FOR(_m, _t, _c) Mutex_lock(MakeSourceLocation(), nullptr, _m, _t)
#else
#define MUTEX_TAKE_LOCK(_m, _t) Mutex_lock(_m, _t)
#define MUTEX_TAKE_LOCK_FOR(_m, _t, _c) Mutex_lock(_m, _t)
#endif // DEBUG

#define MUTEX_UNTAKE_LOCK(_m, _t) Mutex_unlock(_m, _t)
// DEPRECATED DEPRECATED DEPRECATED
/////////////////////////////////////

class EThread;
typedef EThread *EThreadPtr;
typedef volatile EThreadPtr VolatileEThreadPtr;

#if DEBUG
inkcoreapi extern void lock_waiting(const SourceLocation &, const char *handler);
inkcoreapi extern void lock_holding(const SourceLocation &, const char *handler);
inkcoreapi extern void lock_taken(const SourceLocation &, const char *handler);
#endif

/**
  Lock object used in continuations and threads.

  The ProxyMutex class is the main synchronization object used
  throughout the Event System. It is a reference counted object
  that provides mutually exclusive access to a resource. Since the
  Event System is multithreaded by design, the ProxyMutex is required
  to protect data structures and state information that could
  otherwise be affected by the action of concurrent threads.

  A ProxyMutex object has an ink_mutex member (defined in ink_mutex.h)
  which is a wrapper around the platform dependent mutex type. This
  member allows the ProxyMutex to provide the functionallity required
  by the users of the class without the burden of platform specific
  function calls.

  The ProxyMutex also has a reference to the current EThread holding
  the lock as a back pointer for verifying that it is released
  correctly.

  Acquiring/Releasing locks:

  Included with the ProxyMutex class, there are several macros that
  allow you to lock/unlock the underlying mutex object.

*/
class ProxyMutex : public RefCountObj
{
public:
  /**
    Underlying mutex object.

    The platform independent mutex for the ProxyMutex class. You
    must not modify or set it directly.

  */
  // coverity[uninit_member]
  ink_mutex the_mutex;

  /**
    Backpointer to owning thread.

    This is a pointer to the thread currently holding the mutex
    lock.  You must not modify or set this value directly.

  */
  volatile EThreadPtr thread_holding;

  int nthread_holding;

#ifdef DEBUG
  ink_hrtime hold_time;
  SourceLocation srcloc;
  const char *handler;

#ifdef MAX_LOCK_TAKEN
  int taken;
#endif // MAX_LOCK_TAKEN

#ifdef LOCK_CONTENTION_PROFILING
  int total_acquires, blocking_acquires, nonblocking_acquires, successful_nonblocking_acquires, unsuccessful_nonblocking_acquires;
  void print_lock_stats(int flag);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  void free();

  /**
    Constructor - use new_ProxyMutex() instead.

    The constructor of a ProxyMutex object. Initializes the state
    of the object but leaves the initialization of the mutex member
    until it is needed (through init()). Do not use this constructor,
    the preferred mechanism for creating a ProxyMutex is via the
    new_ProxyMutex function, which provides a faster allocation.

  */
  ProxyMutex()
#ifdef DEBUG
    : srcloc(nullptr, nullptr, 0)
#endif
  {
    thread_holding  = nullptr;
    nthread_holding = 0;
#ifdef DEBUG
    hold_time = 0;
    handler   = nullptr;
#ifdef MAX_LOCK_TAKEN
    taken = 0;
#endif // MAX_LOCK_TAKEN
#ifdef LOCK_CONTENTION_PROFILING
    total_acquires                    = 0;
    blocking_acquires                 = 0;
    nonblocking_acquires              = 0;
    successful_nonblocking_acquires   = 0;
    unsuccessful_nonblocking_acquires = 0;
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
    // coverity[uninit_member]
  }

  /**
    Initializes the underlying mutex object.

    After constructing your ProxyMutex object, use this function
    to initialize the underlying mutex object with an optional name.

    @param name Name to identify this ProxyMutex. Its use depends
      on the given platform.

  */
  void
  init(const char *name = "UnnamedMutex")
  {
    ink_mutex_init(&the_mutex);
  }
};

// The ClassAlocator for ProxyMutexes
extern inkcoreapi ClassAllocator<ProxyMutex> mutexAllocator;

inline bool
Mutex_trylock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t)
{
  ink_assert(t != 0);
  ink_assert(t == (EThread *)this_thread());
  if (m->thread_holding != t) {
    if (!ink_mutex_try_acquire(&m->the_mutex)) {
#ifdef DEBUG
      lock_waiting(m->srcloc, m->handler);
#ifdef LOCK_CONTENTION_PROFILING
      m->unsuccessful_nonblocking_acquires++;
      m->nonblocking_acquires++;
      m->total_acquires++;
      m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
      return false;
    }
    m->thread_holding = t;
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->successful_nonblocking_acquires++;
  m->nonblocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline bool
Mutex_trylock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t)
{
  return Mutex_trylock(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t);
}

inline bool
Mutex_trylock_spin(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t, int spincnt = 1)
{
  ink_assert(t != 0);
  if (m->thread_holding != t) {
    int locked;
    do {
      if ((locked = ink_mutex_try_acquire(&m->the_mutex)))
        break;
    } while (--spincnt);
    if (!locked) {
#ifdef DEBUG
      lock_waiting(m->srcloc, m->handler);
#ifdef LOCK_CONTENTION_PROFILING
      m->unsuccessful_nonblocking_acquires++;
      m->nonblocking_acquires++;
      m->total_acquires++;
      m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
      return false;
    }
    m->thread_holding = t;
    ink_assert(m->thread_holding);
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->successful_nonblocking_acquires++;
  m->nonblocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline bool
Mutex_trylock_spin(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t, int spincnt = 1)
{
  return Mutex_trylock_spin(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t, spincnt);
}

inline int
Mutex_lock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t)
{
  ink_assert(t != 0);
  if (m->thread_holding != t) {
    ink_mutex_acquire(&m->the_mutex);
    m->thread_holding = t;
    ink_assert(m->thread_holding);
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->blocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline int
Mutex_lock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t)
{
  return Mutex_lock(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t);
}

inline void
Mutex_unlock(ProxyMutex *m, EThread *t)
{
  if (m->nthread_holding) {
    ink_assert(t == m->thread_holding);
    m->nthread_holding--;
    if (!m->nthread_holding) {
#ifdef DEBUG
      if (Thread::get_hrtime() - m->hold_time > MAX_LOCK_TIME)
        lock_holding(m->srcloc, m->handler);
#ifdef MAX_LOCK_TAKEN
      if (m->taken > MAX_LOCK_TAKEN)
        lock_taken(m->srcloc, m->handler);
#endif // MAX_LOCK_TAKEN
      m->srcloc  = SourceLocation(nullptr, nullptr, 0);
      m->handler = nullptr;
#endif // DEBUG
      ink_assert(m->thread_holding);
      m->thread_holding = 0;
      ink_mutex_release(&m->the_mutex);
    }
  }
}

inline void
Mutex_unlock(Ptr<ProxyMutex> &m, EThread *t)
{
  Mutex_unlock(m.get(), t);
}

/** Scoped lock class for ProxyMutex
 */
class MutexLock
{
private:
  Ptr<ProxyMutex> m;
  bool locked_p;

public:
  MutexLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t)
    : m(am), locked_p(true)
  {
    Mutex_lock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t)
    : m(am), locked_p(true)
  {
    Mutex_lock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  void
  release()
  {
    if (locked_p)
      Mutex_unlock(m, m->thread_holding);
    locked_p = false;
  }

  ~MutexLock() { this->release(); }
};

/** Scoped try lock class for ProxyMutex
 */
class MutexTryLock
{
private:
  Ptr<ProxyMutex> m;
  bool lock_acquired;

public:
  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t)
    : m(am)
  {
    lock_acquired = Mutex_trylock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t)
    : m(am)
  {
    lock_acquired = Mutex_trylock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t, int sp)
    : m(am)
  {
    lock_acquired = Mutex_trylock_spin(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t, sp);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t, int sp)
    : m(am)
  {
    lock_acquired = Mutex_trylock_spin(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t, sp);
  }

  ~MutexTryLock()
  {
    if (lock_acquired)
      Mutex_unlock(m.get(), m->thread_holding);
  }

  /** Spin till lock is acquired
   */
  void
  acquire(EThread *t)
  {
    MUTEX_TAKE_LOCK(m.get(), t);
    lock_acquired = true;
  }

  void
  release()
  {
    ink_assert(lock_acquired); // generate a warning because it shouldn't be done.
    if (lock_acquired) {
      Mutex_unlock(m.get(), m->thread_holding);
    }
    lock_acquired = false;
  }

  bool
  is_locked() const
  {
    return lock_acquired;
  }

  const ProxyMutex *
  get_mutex() const
  {
    return m.get();
  }
};

inline void
ProxyMutex::free()
{
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  print_lock_stats(1);
#endif
#endif
  ink_mutex_destroy(&the_mutex);
  mutexAllocator.free(this);
}

// TODO should take optional mutex "name" identifier, to pass along to the init() fun
/**
  Creates a new ProxyMutex object.

  This is the preferred mechanism for constructing objects of the
  ProxyMutex class. It provides you with faster allocation than
  that of the normal constructor.

  @return A pointer to a ProxyMutex object appropriate for the build
    environment.

*/
inline ProxyMutex *
new_ProxyMutex()
{
  ProxyMutex *m = mutexAllocator.alloc();
  m->init();
  return m;
}

#endif // _Lock_h_
