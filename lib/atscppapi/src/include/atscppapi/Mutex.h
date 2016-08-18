/**
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

/**
 * @file Mutex.h
 * @brief Contains Mutex related classes for creating a Mutex and locking a Mutex in a specific scope.
 */

#pragma once
#ifndef ATSCPPAPI_MUTEX_H_
#define ATSCPPAPI_MUTEX_H_

#include <pthread.h>
#include <atscppapi/noncopyable.h>
#include <atscppapi/shared_ptr.h>

// Import in name only.
typedef struct tsapi_mutex *TSMutex;
typedef struct tsapi_cont *TSCont;

namespace atscppapi
{
/**
 * @brief A mutex is mutual exclusion: a blocking lock.
 *
 * The Mutex class uses pthreads for its implmentation.
 *
 * @see ScopedMutexLock
 * @see ScopedMutexTryLock
 * @see ScopedSharedMutexLock
 * @see ScopedSharedMutexTryLock
 */
class Mutex : noncopyable
{
public:
  /**
   * The available types of Mutexes.
   */
  enum Type {
    TYPE_NORMAL = 0, /**< This type of Mutex will deadlock if locked by a thread already holding the lock */
    TYPE_RECURSIVE,  /**< This type of Mutex will allow a thread holding the lock to lock it again; however, it must be unlocked the
                        same number of times */
    TYPE_ERROR_CHECK /**< This type of Mutex will return errno = EDEADLCK if a thread would deadlock by taking the lock after it
                        already holds it */
  };

  /**
   * Create a mutex
   *
   * @param type The Type of Mutex to create, the default is TYPE_NORMAL.
   * @see Type
   */
  Mutex(Type type = TYPE_NORMAL)
  {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    switch (type) {
    case TYPE_RECURSIVE:
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
      break;
    case TYPE_ERROR_CHECK:
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
      break;
    case TYPE_NORMAL:
    default:
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
      break;
    }

    pthread_mutex_init(&mutex, &attr);
  }

  ~Mutex() { pthread_mutex_destroy(&mutex); }
  /**
   * Try to take the lock, this call will NOT block if the mutex cannot be taken.
   * @return Returns true if the lock was taken, false if it was not. This call obviously will not block.
   */
  bool
  tryLock()
  {
    return !pthread_mutex_trylock(&mutex);
  }

  /**
   * Block until the lock is taken, when this call returns the thread will be holding the lock.
   */
  void
  lock()
  {
    pthread_mutex_lock(&mutex);
  }

  /**
   * Unlock the lock, this call is nonblocking.
   */
  void
  unlock()
  {
    pthread_mutex_unlock(&mutex);
  }

private:
  pthread_mutex_t mutex; /**< Internal mutex identifier */
};

/**
 * @brief Take a Mutex reference and lock inside a scope and unlock when the scope is exited.
 *
 * This is an RAII implementation which will lock a mutex at the start of the
 * scope and unlock it when the scope is exited.
 *
 * @see Mutex
 */
class ScopedMutexLock : noncopyable
{
public:
  /**
   * Create the scoped mutex lock, once this object is constructed the lock will be held by the thread.
   * @param mutex a reference to a Mutex.
   */
  explicit ScopedMutexLock(Mutex &mutex) : mutex_(mutex) { mutex_.lock(); }
  /**
   * Unlock the mutex.
   */
  ~ScopedMutexLock() { mutex_.unlock(); }
private:
  Mutex &mutex_;
};

/**
 * @brief Take a shared_ptr to a Mutex and lock inside a scope and unlock when the scope is exited.
 *
 * This is an RAII implementation which will lock a mutex at the start of the
 * scope and unlock it when the scope is exited.
 *
 * @see Mutex
 */
class ScopedSharedMutexLock : noncopyable
{
public:
  /**
   * Create the scoped mutex lock, once this object is constructed the lock will be held by the thread.
   * @param mutex a shared pointer to a Mutex.
   */
  explicit ScopedSharedMutexLock(shared_ptr<Mutex> mutex) : mutex_(mutex) { mutex_->lock(); }
  /**
   * Unlock the mutex.
   */
  ~ScopedSharedMutexLock() { mutex_->unlock(); }
private:
  shared_ptr<Mutex> mutex_;
};

/**
 * @brief Take a Mutex reference and try to lock inside a scope and unlock when the scope is exited (if the lock was taken).
 *
 * This is an RAII implementation which will lock a mutex at the start of the
 * scope and unlock it when the scope is exited if the lock was taken.
 *
 * @see Mutex
 */
class ScopedMutexTryLock : noncopyable
{
public:
  /**
   * Try to create the scoped mutex lock, if you should check hasLock() to determine if this object was successfully able to take
   * the lock.
   * @param mutex a shared pointer to a Mutex.
   */
  explicit ScopedMutexTryLock(Mutex &mutex) : mutex_(mutex), has_lock_(false) { has_lock_ = mutex_.tryLock(); }
  /**
   * Unlock the mutex (if we hold the lock)
   */
  ~ScopedMutexTryLock()
  {
    if (has_lock_) {
      mutex_.unlock();
    }
  }

  /**
   * @return True if the lock was taken, False if it was not taken.
   */
  bool
  hasLock()
  {
    return has_lock_;
  }

private:
  Mutex &mutex_;
  bool has_lock_;
};

/**
 * @brief Take a shared_ptr to a Mutex and try to lock inside a scope and unlock when the scope is exited (if the lock was taken).
 *
 * This is an RAII implementation which will lock a mutex at the start of the
 * scope and unlock it when the scope is exited if the lock was taken.
 *
 * @see Mutex
 */
class ScopedSharedMutexTryLock : noncopyable
{
public:
  /**
   * Try to create the scoped mutex lock, if you should check hasLock() to determine if this object was successfully able to take
   * the lock.
   * @param mutex a shared pointer to a Mutex.
   */
  explicit ScopedSharedMutexTryLock(shared_ptr<Mutex> mutex) : mutex_(mutex), has_lock_(false) { has_lock_ = mutex_->tryLock(); }
  /**
   * Unlock the mutex (if we hold the lock)
   */
  ~ScopedSharedMutexTryLock()
  {
    if (has_lock_) {
      mutex_->unlock();
    }
  }

  /**
   * @return True if the lock was taken, False if it was not taken.
   */
  bool
  hasLock()
  {
    return has_lock_;
  }

private:
  shared_ptr<Mutex> mutex_;
  bool has_lock_;
};

/**
 * @brief Lock a TS Continuation by acquiring and releasing its lock in the current scope.
 *
 * This is an RAII implementation which will lock a mutex for the continuation at the declaration of an instance
 * and unlock it when the instance goes out of scope.
 */
class ScopedContinuationLock : noncopyable
{
public:
  /**
   * Create the scoped mutex lock, once this object is constructed the lock will be held by the thread.
   * @param cont The TS continuation.
   */
  explicit ScopedContinuationLock(TSCont contp);

  /**
   * Unlock the mutex.
   */
  ~ScopedContinuationLock();

private:
  TSMutex mutex_;
};

} /* atscppapi */

#endif /* ATSCPPAPI_MUTEX_H_ */
