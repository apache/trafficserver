/** @file
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
 * @file test_content_range.cc
 * @brief Unit test for slice ContentRange
 */

#define CATCH_CONFIG_MAIN /* include main function */
#include "../Config.h"
#include "catch.hpp" /* catch unit-test framework */

TEST_CASE("config default", "[AWS][slice][utility]")
{
  Config const config;
  int64_t const defval = Config::blockbytesdefault;
  CHECK(defval == config.m_blockbytes);
}

TEST_CASE("config bytesfrom valid parsing", "[AWS][slice][utility]")
{
  std::vector<std::string> const teststrings = {"1000", "1m", "5g", "2k", "3kb", "1z"};

  std::vector<int64_t> const expvals = {1000, 1024 * 1024, int64_t(1024) * 1024 * 1024 * 5, 1024 * 2, 1024 * 3, 1};

  for (size_t index = 0; index < teststrings.size(); ++index) {
    std::string const &teststr = teststrings[index];
    int64_t const &exp         = expvals[index];
    int64_t const got          = Config::bytesFrom(teststr.c_str());

    CHECK(got == exp);
    if (got != exp) {
      INFO(teststr.c_str());
    }
  }
}

TEST_CASE("config bytesfrom invalid parsing", "[AWS][slice][utility]")
{
  std::vector<std::string> const badstrings = {
    "abc", // alpha
    "g00", // giga
    "M00", // mega
    "k00", // kilo
    "-500" // negative
  };

  for (std::string const &badstr : badstrings) {
    int64_t const val = Config::bytesFrom(badstr.c_str());
    CHECK(0 == val);
    if (0 != val) {
      INFO(badstr.c_str());
    }
  }
}
