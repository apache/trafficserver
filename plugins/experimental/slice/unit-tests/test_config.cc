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

#include <array>
#include <getopt.h>

TEST_CASE("config default", "[AWS][slice][utility]")
{
  Config const config;
  int64_t const defval = Config::blockbytesdefault;
  CHECK(defval == config.m_blockbytes);
}

TEST_CASE("config bytesfrom valid parsing", "[AWS][slice][utility]")
{
  static std::array<std::string, 6> const teststrings = {{
    "1000",
    "1m",
    "5g",
    "2k",
    "3kb",
    "1z",
  }};

  constexpr std::array<int64_t, 6> const expvals = {{
    1000,
    1024 * 1024,
    int64_t(1024) * 1024 * 1024 * 5,
    1024 * 2,
    1024 * 3,
    1,
  }};

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
  static std::array<std::string, 5> const badstrings = {{
    "abc",  // alpha
    "g00",  // giga
    "M00",  // mega
    "k00",  // kilo
    "-500", // negative
  }};

  for (std::string const &badstr : badstrings) {
    int64_t const val = Config::bytesFrom(badstr.c_str());
    CHECK(0 == val);
    if (0 != val) {
      INFO(badstr.c_str());
    }
  }
}

TEST_CASE("config fromargs validate sizes", "[AWS][slice][utility]")
{
  char const *const appname = "slice.so";
  int64_t blockBytesMax = 128 * 1024 * 1024, blockBytesMin = 256 * 1024;

  CHECK(blockBytesMax == Config::blockbytesmax);
  CHECK(blockBytesMin == Config::blockbytesmin);

  std::vector<std::string> const argkws                 = {"-b ", "--blockbytes=", "blockbytes:"};
  std::vector<std::pair<std::string, bool>> const tests = {{"4m", true},
                                                           {"1", false},
                                                           {"32m", true},
                                                           {"64m", true},
                                                           {"256k", true},
                                                           {"128m", true},
                                                           {"10m", true},
                                                           {std::to_string(blockBytesMax), true},
                                                           {std::to_string(blockBytesMax + 1), false},
                                                           {std::to_string(blockBytesMax - 1), true},
                                                           {std::to_string(blockBytesMin), true},
                                                           {std::to_string(blockBytesMin + 1), true},
                                                           {std::to_string(blockBytesMin - 1), false}};

  for (std::string const &kw : argkws) { // test each argument keyword with each test pair
    for (std::pair<std::string, bool> const &test : tests) {
      // getopt uses global variables; ensure the index is reset each iteration
      optind = 0;

      // set up args
      std::vector<char *> argv;
      std::string arg = kw + test.first;
      argv.push_back((char *)appname);
      argv.push_back((char *)arg.c_str());

      // configure slice
      Config *const config = new Config;
      config->fromArgs(argv.size(), argv.data());

      // validate that the configured m_blockbytes are what we expect
      CHECK(test.second == (config->m_blockbytes != config->blockbytesdefault));

      // failed; print additional info
      if (test.second != (config->m_blockbytes != config->blockbytesdefault)) {
        INFO(test.first.c_str());
        INFO(config->m_blockbytes);
      }

      // validate that the result of bytesFrom aligns with the current value of config->m_blockbytes as expected
      int64_t const blockbytes = config->bytesFrom(test.first.c_str());
      CHECK(test.second == (config->m_blockbytes == blockbytes));

      // failed; print additional info
      if (test.second != (config->m_blockbytes == blockbytes)) {
        INFO(blockbytes);
        INFO(config->m_blockbytes);
      }

      delete config;
    }
  }
}
