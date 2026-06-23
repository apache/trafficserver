/** @file

  Clang Thread Safety Analysis annotation macros.

  These wrap Clang's @c -Wthread-safety attributes so a lock's contract can be
  expressed in the type system: which mutex guards which data, and which lock a
  function requires its caller to hold. The compiler then proves, at build time,
  that guarded data is only touched while the right lock is held.

  The annotations are compile-time only. Under Clang they expand to
  @c __attribute__((...)) consumed by the analysis; under every other compiler
  they expand to nothing. They generate no code and have zero runtime cost.

  Important note: Clang's analysis only tracks lock state through types marked as
  capabilities (the mutex) and scoped capabilities (the RAII guard). The @c std::
  lock wrappers are deliberately flexible -- deferred locking, adopt/release, and
  movability let the held state escape a single scope -- which makes it impossible
  to track statically, and ATS does not need that flexibility. Annotated code
  therefore takes its locks through ATS-owned annotated types -- @c ts::mutex with
  @c ts::scoped_lock, or @c ts::shared_mutex with @c ts::scoped_writer_lock /
  @c ts::scoped_reader_lock -- whose simple acquire-in-constructor /
  release-in-destructor contract the analysis can follow.

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

#if defined(__clang__)
#define TS_THREAD_ANNOTATION(x) __attribute__((x))
#else
#define TS_THREAD_ANNOTATION(x)
#endif

/// Mark a class as a capability (a lock-like object that can be "held").
#define TS_CAPABILITY(name) TS_THREAD_ANNOTATION(capability(name))

/// Mark an RAII type whose lifetime holds a capability (acquire in ctor,
/// release in dtor), e.g. a scoped lock guard.
#define TS_SCOPED_CAPABILITY TS_THREAD_ANNOTATION(scoped_lockable)

/// The annotated data member may only be accessed while @a x is held.
#define TS_GUARDED_BY(x) TS_THREAD_ANNOTATION(guarded_by(x))

/// The data pointed to by the annotated pointer may only be accessed while
/// @a x is held.
#define TS_PT_GUARDED_BY(x) TS_THREAD_ANNOTATION(pt_guarded_by(x))

/// The function acquires the listed capabilities (exclusively / shared).
#define TS_ACQUIRE(...)        TS_THREAD_ANNOTATION(acquire_capability(__VA_ARGS__))
#define TS_ACQUIRE_SHARED(...) TS_THREAD_ANNOTATION(acquire_shared_capability(__VA_ARGS__))

/// The function releases the listed capabilities. Use the plain form (not the
/// shared form) on a scoped-capability destructor even for a shared guard.
#define TS_RELEASE(...)        TS_THREAD_ANNOTATION(release_capability(__VA_ARGS__))
#define TS_RELEASE_SHARED(...) TS_THREAD_ANNOTATION(release_shared_capability(__VA_ARGS__))

/// The function conditionally acquires a capability, holding it only on the
/// branch where it returns @a success_value.
#define TS_TRY_ACQUIRE(...)        TS_THREAD_ANNOTATION(try_acquire_capability(__VA_ARGS__))
#define TS_TRY_ACQUIRE_SHARED(...) TS_THREAD_ANNOTATION(try_acquire_shared_capability(__VA_ARGS__))

/// The caller must already hold the listed capabilities (exclusively / shared).
#define TS_REQUIRES(...)        TS_THREAD_ANNOTATION(requires_capability(__VA_ARGS__))
#define TS_REQUIRES_SHARED(...) TS_THREAD_ANNOTATION(requires_shared_capability(__VA_ARGS__))

/// The caller must NOT hold the listed capabilities (prevents self-deadlock).
#define TS_EXCLUDES(...) TS_THREAD_ANNOTATION(locks_excluded(__VA_ARGS__))

/// Declare that a getter returns a reference to the named capability.
#define TS_RETURN_CAPABILITY(x) TS_THREAD_ANNOTATION(lock_returned(x))

/// Tell the analysis a capability is held here without acquiring it (runtime
/// assertion form).
#define TS_ASSERT_CAPABILITY(x)        TS_THREAD_ANNOTATION(assert_capability(x))
#define TS_ASSERT_SHARED_CAPABILITY(x) TS_THREAD_ANNOTATION(assert_shared_capability(x))

/// Disable the analysis for a single function. Use sparingly, for code the
/// analysis cannot model (recursive acquire, hand-off across threads,
/// single-threaded destructors).
#define TS_NO_THREAD_SAFETY_ANALYSIS TS_THREAD_ANNOTATION(no_thread_safety_analysis)
