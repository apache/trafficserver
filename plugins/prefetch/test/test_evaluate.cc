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

TEST_CASE("Basic computation works", "[evaluate]")
{
  REQUIRE(evaluate("1+3") == "4");
  REQUIRE(evaluate("5-2") == "3");
}

TEST_CASE("Empty expression", "[empty]")
{
  REQUIRE(evaluate("") == "");
}

TEST_CASE("64-bit result works", "[evaluate64]")
{
  REQUIRE(evaluate("4294967295+4294967295") == "8589934590");
}

TEST_CASE("Larger number saturation", "[saturation]")
{
  REQUIRE(evaluate("3842948374928374982374982374") == "4294967295");
  REQUIRE(evaluate("3248739487239847298374738924-4294967295") == "0");
}

TEST_CASE("Negative subtraction", "[negative]")
{
  REQUIRE(evaluate("24-498739847") == "0");
}

TEST_CASE("Treat invalid number as zero", "[invalid]")
{
  REQUIRE(evaluate("foobar") == "0");
  REQUIRE(evaluate("foo+bar") == "0");
  REQUIRE(evaluate("3+bar") == "3");
}

TEST_CASE("Padding with leading zeroes", "[padding]")
{
  REQUIRE(evaluate("5:1+2") == "00003");
  REQUIRE(evaluate("2:123+123") == "246");
}
