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

/**
 * @file test_plugin.cc
 * @brief Unit tests for plugin.cc
 */

#define CATCH_CONFIG_MAIN
#include <catch.hpp>
#include "../evaluate.h"
#include <limits>

TEST_CASE("Basic computation works", "[evaluate]")
{
  REQUIRE(evaluate("1+3") == "4");
  REQUIRE(evaluate("5-2") == "3");
  REQUIRE(evaluate("1+3", EvalPolicy::Bignum) == "4");
  REQUIRE(evaluate("5-2", EvalPolicy::Bignum) == "3");
}

TEST_CASE("Empty expression", "[empty]")
{
  REQUIRE(evaluate("") == "");
  REQUIRE(evaluate("", EvalPolicy::Bignum) == "");
}

TEST_CASE("64-bit result works", "[evaluate64]")
{
  const uint32_t max32    = std::numeric_limits<uint32_t>::max();
  const String   max32str = std::to_string(max32);
  REQUIRE(evaluate(max32str + "+" + max32str) == "8589934590");

  const uint64_t max32_64 = max32;
  const String   addedstr = std::to_string(2 * max32_64);
  REQUIRE(evaluate(max32str + "+" + max32str, EvalPolicy::Bignum) == addedstr);
}

TEST_CASE("Larger number 32-bit saturation", "[saturation]")
{
  const uint32_t max32    = std::numeric_limits<uint32_t>::max();
  const String   max32str = std::to_string(max32);
  REQUIRE(evaluate("3842948374928374982374982374") == max32str);
  REQUIRE(evaluate("3248739487239847298374738924-" + max32str) == "0");
}

TEST_CASE("Larger number 64-bit saturation", "[saturation]")
{
  const uint64_t max64    = std::numeric_limits<uint64_t>::max();
  const String   max64str = std::to_string(max64);
  REQUIRE(evaluate("3842948374928374982374982374", EvalPolicy::Overflow64) == max64str);
  REQUIRE(evaluate("3248739487239847298374738924-" + max64str, EvalPolicy::Overflow64) == "0");
}

TEST_CASE("Larger number bignum no saturation", "[saturation]")
{
  REQUIRE(evaluate("3842948374928374982374982374+6842948374928374982374982374", EvalPolicy::Bignum) ==
          "10685896749856749964749964748");
  REQUIRE(evaluate("3248739487239847298374738924-3248739487239847298374738923", EvalPolicy::Bignum) == "1");
  REQUIRE(evaluate("1000000000000000000000000000-1", EvalPolicy::Bignum) == "999999999999999999999999999");
}

TEST_CASE("Negative subtraction", "[negative]")
{
  REQUIRE(evaluate("24-498739847") == "0");
  REQUIRE(evaluate("24-498739847", EvalPolicy::Overflow64) == "0");
  REQUIRE(evaluate("24-498739847", EvalPolicy::Bignum) == "0");
}

TEST_CASE("Treat invalid number as zero", "[invalid]")
{
  REQUIRE(evaluate("foobar") == "0");
  REQUIRE(evaluate("foobar", EvalPolicy::Bignum) == "0");
  REQUIRE(evaluate("foo+bar") == "0");
  REQUIRE(evaluate("foobar+bar", EvalPolicy::Bignum) == "0");
  REQUIRE(evaluate("3+bar") == "3");
  REQUIRE(evaluate("3+bar", EvalPolicy::Bignum) == "3");
}

TEST_CASE("Padding with leading zeroes", "[padding]")
{
  REQUIRE(evaluate("5:1+2") == "00003");
  REQUIRE(evaluate("5:1+2", EvalPolicy::Bignum) == "00003");
  REQUIRE(evaluate("2:123+123") == "246");
  REQUIRE(evaluate("2:123+123", EvalPolicy::Bignum) == "246");
}
