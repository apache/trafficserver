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

/***************************************************************************
Assertions

***************************************************************************/
#pragma once

#include "tscore/ink_apidefs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* don't use assert, no really DON'T use assert */
#undef assert
#define assert __DONT_USE_BARE_assert_USE_ink_assert__
#undef _ASSERT_H
#define _ASSERT_H
#undef __ASSERT_H__
#define __ASSERT_H__

void _ink_assert(const char *a, const char *f, int l) TS_NORETURN;

#if defined(DEBUG) || defined(ENABLE_ALL_ASSERTS) || defined(__clang_analyzer__) || defined(__COVERITY__)
#define ink_assert(EX) ((void)(__builtin_expect(!!(EX), 1) ? (void)0 : _ink_assert(#EX, __FILE__, __LINE__)))
#else
#define ink_assert(EX) (void)(EX)
#endif

#define ink_release_assert(EX) ((void)(__builtin_expect(!!(EX), 0) ? (void)0 : _ink_assert(#EX, __FILE__, __LINE__)))

#ifdef __cplusplus
}

/*
Use cast_to_derived() to cast a pointer/reference to a dynamic base class into a pointer/reference to a class
that inherits directly or indirectly from the base class.  Uses checked dynamic_cast in debug builds, and
static_cast in release (optimized) builds.  Use examples:

class A { public: virtual ~A(); };
class B : public A {};
B * foo(A *a) { return cast_to_derived<B>(a); }
B & foo2(A &a) { return cast_to_derived<B>(a); }
B const & foo3(A const &a) { return cast_to_derived<B const>(a); }

*/

template <class Derived, class Base>
Derived *
cast_to_derived(Base *b) // b must not be nullptr.
{
#ifdef DEBUG

  ink_assert(b != nullptr);
  auto d = dynamic_cast<Derived *>(b);
  ink_assert(d != nullptr);
  return d;

#else

  return static_cast<Derived *>(b);

#endif
}

template <class Derived, class Base>
Derived &
cast_to_derived(Base &b)
{
#ifdef DEBUG

  return dynamic_cast<Derived &>(b);

#else

  return static_cast<Derived &>(b);

#endif
}

#endif /* __cplusplus */
