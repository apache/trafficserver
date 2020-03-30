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
#include "catch.hpp"      /* catch unit-test framework */
#include "../Range.h"

TEST_CASE("range invalid state", "[AWS][slice][utility]")
{
  CHECK_FALSE(Range().isValid());           // null range
  CHECK_FALSE(Range(1024, 1024).isValid()); // zero range
  CHECK_FALSE(Range(-5, 13).isValid());     // negative start
}

TEST_CASE("range to/from string - valid", "[AWS][slice][utility]")
{
  std::vector<std::string> const teststrings = {
    "bytes=0-1023", // start at zero
    "bytes=1-1024", // start from non zero
    "bytes=11-11",  // single byte
    "bytes=1-",     // 2nd byte to end
    "bytes=3-17",   // ,23-29" // open
    "bytes=3 -17 ", //,18-29" // adjacent
    "bytes=3- 17",  //, 11-29" // overlapping
    "bytes=3 - 11", //,13-17 , 23-29" // unsorted triplet
    "bytes=3-11 ",  //,13-17, 23-29" // unsorted triplet
    "bytes=0-0",    //,-1" // first and last bytes
    "bytes=-20",    // last 20 bytes of file
  };

  std::vector<Range> const exps = {
    Range{0, 1023 + 1},      //
    Range{1, 1024 + 1},      //
    Range{11, 11 + 1},       //
    Range{1, Range::maxval}, //
    Range{3, 17 + 1},        //
    Range{3, 17 + 1},        //
    Range{3, 17 + 1},        //
    Range{3, 11 + 1},        //
    Range{3, 11 + 1},        //
    Range{0, 1},             //
    Range{-20, 0}            //
  };

  for (size_t index = 0; index < teststrings.size(); ++index) {
    std::string const &str = teststrings[index];

    Range got;
    CHECK(got.fromStringClosed(str.c_str()));
    CHECK(got.isValid());

    if (!got.isValid()) {
      INFO(str.c_str());
    }

    Range const &exp = exps[index];
    CHECK(got.m_beg == exp.m_beg);
    CHECK(got.m_end == exp.m_end);
  }
}

TEST_CASE("range from string - invalid")
{
  std::vector<std::string> const badstrings = {
    "Range: bytes=-13",   // malformed
    "bytes=-60-50",       // first negative, second nonzero
    "bytes=17-13",        // degenerate
    "bytes 0-1023/146515" // malformed
  };

  Range range;
  for (std::string const &badstr : badstrings) {
    CHECK_FALSE(range.fromStringClosed(badstr.c_str()));
    CHECK_FALSE(range.isValid());
    if (range.isValid()) {
      INFO(badstr.c_str());
    }
  }
}
