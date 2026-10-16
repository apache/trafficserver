/*
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

/** @file test_TextViewConstructors.cc
    Constructor overload resolution tests for TextView.

    Verifies correct behavior of the TextView pointer-and-length constructors
    across different integral types. This focuses on verifying correct overload
    resolution for aliased types (e.g., size_t/unsigned) and ensures the -1
    sentinel behavior works consistently across all signed integral template
    instantiations.
*/

#include <cstddef>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include "swoc/TextView.h"

using swoc::TextView;

// ============================================================================
// Template-driven Cross-Platform Tests
// ============================================================================

TEMPLATE_TEST_CASE(
    "TextView: Integral length constructors resolve without ambiguity",
    "[libswoc][TextView][Constructors][CrossPlatform]",
    size_t, int, long, long long, short, unsigned, unsigned long)
{
  const char* str = "Hello World";

  SECTION("Positive length constructs a view of exactly that size")
  {
    // Explicitly casting to TestType forces the compiler to select the
    // constructor overload for that specific integral type.
    TextView tv(str, static_cast<TestType>(5));

    REQUIRE(tv.length() == 5);
    REQUIRE(tv == "Hello");

    // Verify that TextView references the original buffer rather than
    // copying it.
    REQUIRE(tv.data() == str);
  }

  SECTION("Zero length constructs an empty view")
  {
    TextView tv(str, static_cast<TestType>(0));

    REQUIRE(tv.empty());
    REQUIRE(tv.length() == 0);
    REQUIRE(tv.data() == str);
  }
}

// ============================================================================
// Special Value Handling (int/ssize_t specific)
// ============================================================================

TEST_CASE("TextView: Negative length triggers strlen",
          "[libswoc][TextView][Constructors][NegativeLen]")
{
  const char* str = "Implicit Length";

  // Passing -1 to the int constructor is a specific API feature
  // instructing the view to calculate length via strlen.
  TextView tv(str, -1);

  REQUIRE(tv == str);
  REQUIRE(tv.length() == std::strlen(str));
  REQUIRE(tv.data() == str);
}

// ============================================================================
// Nullptr Safety
// ============================================================================

TEST_CASE("TextView: Nullptr results in empty view regardless of length",
          "[libswoc][TextView][Constructors][Null]")
{
  // Regardless of what length is passed, a null data pointer must
  // result in a safe, empty view.
  SECTION("With size_t length")
  {
    TextView tv(nullptr, static_cast<size_t>(100));
    REQUIRE(tv.empty());
    REQUIRE(tv.data() == nullptr);
  }

  SECTION("With int length")
  {
    TextView tv(nullptr, 100);
    REQUIRE(tv.empty());
    REQUIRE(tv.data() == nullptr);
  }

  SECTION("With negative int length")
  {
    TextView tv(nullptr, -1);
    REQUIRE(tv.empty());
    REQUIRE(tv.data() == nullptr);
  }
}

// ============================================================================
// Overload Resolution Regression Tests
// ============================================================================

// Regression test for size_t / unsigned int overload resolution. These types
// can be aliased on certain platforms, making this a crucial disambiguation check.
TEST_CASE("TextView: size_t/unsigned overload disambiguation",
          "[libswoc][TextView][Constructors][OverloadRegression]")
{
  const char* str = "Unsigned Test";

  TextView tv_size_t(str, static_cast<size_t>(9));
  TextView tv_unsigned(str, static_cast<unsigned int>(9));

  REQUIRE(tv_size_t == tv_unsigned);
  REQUIRE(tv_size_t.length() == tv_unsigned.length());
  REQUIRE(tv_size_t.data() == tv_unsigned.data());
}

// Regression test for ssize_t / int overload resolution. These types can
// be aliased on certain platforms, making this a crucial disambiguation check.
TEST_CASE("TextView: ssize_t/int overload disambiguation",
          "[libswoc][TextView][Constructors][OverloadRegression]")
{
  const char* str = "Signed Test";

  TextView tv_ssize_t(str, static_cast<ssize_t>(5));
  TextView tv_int(str, static_cast<int>(5));

  REQUIRE(tv_ssize_t == tv_int);
  REQUIRE(tv_ssize_t.data() == tv_int.data());
}

// ============================================================================
// Signed Integral Sentinel Semantics
// ============================================================================

// Positive values are already covered by the generic template test.
// This regression verifies that the special -1 sentinel semantics are
// preserved for all signed integral template instantiations.
TEMPLATE_TEST_CASE(
    "TextView: Template signed integral constructor preserves -1 sentinel",
    "[libswoc][TextView][Constructors][Sentinel]",
    short, ssize_t, long, long long, std::ptrdiff_t)
{
  const char* str = "Implicit Length";

  // Signed integral constructors implemented via the template overload
  // must preserve the -1 sentinel semantics used by TextView.
  TextView tv(str, static_cast<TestType>(-1));

  REQUIRE(tv == str);
  REQUIRE(tv.length() == std::strlen(str));
  REQUIRE(tv.data() == str);
}