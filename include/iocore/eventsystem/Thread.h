/** @file

  The @c Thread base class for OS threads owned by ATS, and the means to
  start one and to look up the @c Thread registered for the calling thread.

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

#pragma once

#include <functional>

#include "iocore/eventsystem/ProxyAllocator.h"
#include "tscore/Ptr.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_thread.h"

class ProxyMutex;

/**
  Size in bytes, including the terminating NUL, of a thread name buffer.

  Use it to size buffers that build or read back the names of ATS
  threads. A thread name holds at most @c MAX_THREAD_NAME_LENGTH - 1
  bytes; longer names are truncated when the thread is started, without
  any error being reported.
*/
constexpr int MAX_THREAD_NAME_LENGTH = 16;

/**
  Optional entry point for the OS thread spawned by @c Thread::start.

  When non-empty, the spawned thread invokes the callable exactly once,
  in place of @c Thread::execute, and exits when it returns.

  @note @c Thread::start keeps its own copy of the callable. Anything the
        callable captures by reference or pointer MUST stay valid until
        the callable returns.

  @pre The callable MUST NOT exit via an exception.

  @par Thread Safety
  The callable runs on the spawned thread, concurrently with the caller
  of @c Thread::start. State it shares with other threads needs its own
  synchronization.
*/
using ThreadFunction = std::function<void()>;

/**
  Base class for an object that represents one OS thread and holds its
  per-thread state.

  A @c Thread represents the OS thread on which it is registered in
  @c this_thread_ptr.

  @par Lifetime
  A @c Thread MUST NOT be destroyed while a different OS thread may
  still use it, for example through @c this_thread_ptr or a running
  @c execute.
*/
class Thread
{
public:
  /**
    Identifier of the OS thread spawned by @c start.

    Zero until @c start is called, and remains zero for a @c Thread that
    is registered via @c set_specific without being started. The value
    is never reset: once the OS thread has been joined, it no longer
    identifies a live thread and MAY be reused by the platform.

    @par Thread Safety
    Written by @c start on its calling thread before @c start returns.
    Safe to read from that thread after @c start returns, and from any
    thread that synchronizes with it afterward. The spawned thread
    itself MUST NOT read @c tid without such synchronization; it should
    obtain its own identifier from the platform instead.
  */
  // NOLINTNEXTLINE(modernize-use-nullptr)
  ink_thread tid = 0;

  /**
    Mutex that the @c Thread holds on its own behalf.

    Non-null and unchanged from construction until destruction; do not
    assign to it. Once construction completes, the @c Thread holds this
    mutex until destruction, so a lock taken on the OS thread this
    @c Thread represents always succeeds immediately. Any object that
    adopts this mutex as its own lock can therefore be locked, and its
    work run, only on that OS thread.

    @note Do not lock it from any other thread. A try-lock always
          fails, and a blocking lock is undefined.

    @par Thread Safety
    Reading or copying the @c Ptr is safe from any thread.
  */
  Ptr<ProxyMutex> mutex;

  /**
    Registers @c this as the calling OS thread's @c Thread.

    A @c Thread that spawns its own OS thread registers itself on that
    thread before its entry point runs. Call this directly only to
    register a @c Thread for an OS thread it did not spawn, such as the
    main thread. Overrides MAY register additional per-subclass
    state for the calling thread.

    Registration replaces any @c Thread previously registered on the
    calling thread.

    @pre  @c this is not registered on any other running OS thread.

    @post @c this_thread_ptr on the calling thread equals @c this.

    @par Thread Safety
    Thread-safe.
  */
  virtual void
  set_specific()
  {
    this_thread_ptr = this;
  }

  /**
    The @c Thread registered for the calling OS thread, or nullptr if
    none is registered.

    Every OS thread starts with nullptr. A @c Thread that starts an OS
    thread registers itself on it before the thread's entry point runs.
    Registering another @c Thread replaces the value; destroying the
    registered @c Thread on this OS thread resets it to nullptr, not to
    any earlier value. Consumers MUST NOT assign to it.

    @note Destroying the registered @c Thread on a different OS thread
          leaves this pointer dangling on the OS thread it was
          registered for.

    @par Thread Safety
    Thread-safe.
  */
  static thread_local Thread *this_thread_ptr;

  /**
    @name Per-thread allocator freelists

    Each field is this thread's freelist for the global allocator of
    the same name. Consumers MUST access the fields only through the
    @c THREAD_ALLOC and @c THREAD_FREE family of macros, which select
    the field by that name. An object MAY be freed through a different
    @c Thread than the one it was allocated through.

    @note To use these macros with a new global allocator, add a field
          here with exactly that allocator's name.

    @par Thread Safety
    Not thread-safe. The @c Thread passed to the macros MUST be the
    calling OS thread's own @c Thread.
  */
  /// @{
  ProxyAllocator eventAllocator;
  ProxyAllocator netVCAllocator;
  ProxyAllocator sslNetVCAllocator;
  ProxyAllocator quicNetVCAllocator;
  ProxyAllocator http1ClientSessionAllocator;
  ProxyAllocator http2ClientSessionAllocator;
  ProxyAllocator http2ServerSessionAllocator;
  ProxyAllocator http2StreamAllocator;
  ProxyAllocator httpSMAllocator;
  ProxyAllocator quicClientSessionAllocator;
  ProxyAllocator httpServerSessionAllocator;
  ProxyAllocator hdrHeapAllocator;
  ProxyAllocator strHeapAllocator;
  ProxyAllocator cacheVConnectionAllocator;
  ProxyAllocator cacheEvacuateDocVConnectionAllocator;
  ProxyAllocator openDirEntryAllocator;
  ProxyAllocator ramCacheCLFUSEntryAllocator;
  ProxyAllocator ramCacheLRUEntryAllocator;
  ProxyAllocator ramCacheS3FIFOEntryAllocator;
  ProxyAllocator evacuationBlockAllocator;
  ProxyAllocator ioDataAllocator;
  ProxyAllocator ioAllocator;
  ProxyAllocator ioBlockAllocator;
  ProxyAllocator preWarmSMAllocator;
  // From InkAPI (plugins wrappers)
  ProxyAllocator apiHookAllocator;
  ProxyAllocator INKContAllocator;
  ProxyAllocator INKVConnAllocator;
  ProxyAllocator mHandleAllocator;
#if TS_HAS_CRIPTS == 1
  ProxyAllocator criptContextAllocator;
#endif
  /// @}

  /**
    Spawns the OS thread for this @c Thread.

    The new thread calls @c set_specific, sets its name to @p name, and
    then calls @p f if it is non-empty, or @c execute otherwise. The OS
    thread exits when that call returns. If the OS thread cannot be
    created, the process aborts.

    @param[in] name      Thread name. It is copied; bytes past the first
                         @c MAX_THREAD_NAME_LENGTH - 1 are dropped.
    @param[in] stack     Stack for the new thread, or nullptr to let the
                         platform allocate one.
    @param[in] stacksize Stack size in bytes, or 0 for a build-configured
                         default.
    @param[in] f         Callable to run instead of @c execute. It is
                         copied.

    @pre  @c start has not been called on this @c Thread before.
    @pre  @p name is a non-null, NUL-terminated string.
    @pre  If @p stack is non-null, it spans at least as many bytes as
          the stack size and stays valid until the OS thread exits.
    @pre  This @c Thread stays alive until the OS thread exits.

    @post @c tid identifies the new OS thread. The new thread may
          already be running, or may have finished.

    @note If the platform rejects @p stack or the stack size (for
          example, because of alignment or a minimum size), which stack
          the thread gets is unspecified.

    @par Thread Safety
    Safe to call concurrently on different @c Thread objects. The write
    to @c tid is not synchronized with the new thread, so code running
    on the new thread MUST NOT read @c tid.
  */
  void start(const char *name, void *stack, size_t stacksize, ThreadFunction const &f = ThreadFunction());

  /**
    Runs the thread's main body.

    Subclasses override this to implement the thread's role. When the
    thread is started without an entry-point callable, the framework
    invokes this on the newly spawned OS thread, and that OS thread
    exits once this returns.

    @pre  @c set_specific has been invoked on this instance from the
          calling OS thread.
    @pre  This method has not previously been invoked on this instance.

    @par Thread Safety
    Not thread-safe.
  */
  virtual void execute() = 0;

  Thread(const Thread &)            = delete;
  Thread &operator=(const Thread &) = delete;

  /**
    Destroys the @c Thread without joining or signaling its OS thread.

    @pre  If this instance was started, the OS thread backing it has
          exited.
    @pre  No running OS thread other than the caller has its
          @c this_thread_ptr pointing at this instance.

    @post If the caller's @c this_thread_ptr pointed at this instance,
          it is nullptr.

    @par Thread Safety
    Not thread-safe.
  */
  virtual ~Thread();

protected:
  /**
    Constructs a @c Thread that has no OS thread yet.

    @pre  Not called during static initialization.

    @post @c mutex refers to a newly allocated, unlocked mutex that no
          other object shares. @c this_thread_ptr is unchanged; the new
          @c Thread is not registered on any OS thread.

    @par Thread Safety
    Thread-safe.
  */
  Thread();
};

/**
  Returns the @c Thread registered for the calling OS thread.

  @return The registered @c Thread, or nullptr if none is registered.
          An OS thread spawned by a @c Thread has that @c Thread
          registered before any of its work runs; any other OS thread
          has none unless one is registered for it explicitly.

  @par Thread Safety
  Thread-safe.
*/
inline Thread *
this_thread()
{
  return Thread::this_thread_ptr;
}
