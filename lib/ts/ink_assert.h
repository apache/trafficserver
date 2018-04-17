/** @file

  Support explicitly causing a core dump when invariant checks fail.

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

/***************************************************************************
Assertions

***************************************************************************/
#pragma once

#include "ts/ink_apidefs.h"
#include "ts/ink_error.h"

/* don't use assert, no really DON'T use assert */
#undef assert
#define assert __DONT_USE_BARE_assert_USE_ink_assert__
#undef _ASSERT_H
#define _ASSERT_H
#undef __ASSERT_H__
#define __ASSERT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

inkcoreapi void _ink_assert(const char *a, const char *f, int l) TS_NORETURN;

#if defined(DEBUG) || defined(__clang_analyzer__) || defined(__COVERITY__)
#define ink_assert(EX) ((void)(likely(EX) ? (void)0 : _ink_assert(#EX, __FILE__, __LINE__)))
#else
#define ink_assert(EX) (void)(EX)
#endif

#define ink_release_assert(EX) ((void)(likely(EX) ? (void)0 : _ink_assert(#EX, __FILE__, __LINE__)))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef __cplusplus

#include <type_traits>

#include "ts/debug.h"

namespace ts
{
namespace _private_
{
  inline void
  ts_assert_(std::false_type, const char *, const char *, int)
  {
  }

  inline void
  ts_assert_(std::true_type, const char *a, const char *f, int l)
  {
    _ink_assert(a, f, l);
  }
}
}

#define ts_assert(EX) (likely(EX) ? static_cast<void>(0) : ts::_private_::ink_assert_(ts::TestModeType(), #EX, __FILE__, __LINE__))

#define ts_release_assert(EX) \
  (likely(EX) ? static_cast<void>(0) : ts::_private_::ink_assert_(std::true_type(), #EX, __FILE__, __LINE__))

#endif /* __cplusplus */

/* workaround a bug in the  stupid Sun preprocessor

#undef assert
#define assert __DONT_USE_BARE_assert_USE_ink_assert__
#define _ASSERT_H
#undef __ASSERT_H__
#define __ASSERT_H__
*/
