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

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#else
#ifndef likely
#define likely(x) (x)
#endif
#ifndef unlikely
#define unlikely(x) (x)
#endif
#endif

#if !defined(TS_NORETURN)
#if defined(__GNUC__) || defined(__clang__)
#define TS_NORETURN __attribute__((noreturn))
#else
#define TS_NORETURN
#endif
#endif

/*  Enable this to get printf() style warnings on the Inktomi functions. */
/* #define PRINTFLIKE(IDX, FIRST)  __attribute__((format (printf, IDX, FIRST))) */
/** For further information, see the doxygen comments for TS_PRINTFLIKE in
 * include/ts/apidefs.h.in */
#if !defined(TS_PRINTFLIKE)
#if defined(__GNUC__) || defined(__clang__)
#define TS_PRINTFLIKE(fmt_index, arg_index) __attribute__((format(printf, fmt_index, arg_index)))
#else
#define TS_PRINTFLIKE(fmt_index, arg_index)
#endif
#endif

#if !defined(TS_NONNULL)
#if defined(__GNUC__) || defined(__clang__)
#define TS_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define TS_NONNULL(...)
#endif
#endif

#if !defined(TS_INLINE)
#define TS_INLINE inline
#endif
