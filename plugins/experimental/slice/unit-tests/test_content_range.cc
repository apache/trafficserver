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
#include "../ContentRange.h"

TEST_CASE("content_range invalid state", "[AWS][slice][utility]")
{
  CHECK_FALSE(ContentRange().isValid());                 // null range
  CHECK_FALSE(ContentRange(1024, 1024, 4000).isValid()); // zero range
  CHECK_FALSE(ContentRange(0, 1024, 1023).isValid());    // past end
  CHECK_FALSE(ContentRange(-5, 13, 40).isValid());       // negative start
}

TEST_CASE("content_range to/from string - valid", "[AWS][slice][utility]")
{
  ContentRange const exprange(1023, 1048576, 307232768);

  CHECK(exprange.isValid());

  std::string const expstr("bytes 1023-1048575/307232768");

  char gotbuf[1024];
  int gotlen = sizeof(gotbuf);

  bool const strstat(exprange.toStringClosed(gotbuf, &gotlen));

  CHECK(strstat);
  CHECK(gotlen == static_cast<int>(expstr.size()));
  CHECK(expstr == std::string(gotbuf));

  ContentRange gotrange;
  bool const gotstat(gotrange.fromStringClosed(expstr.c_str()));

  CHECK(gotstat);
  CHECK(gotrange.m_beg == exprange.m_beg);
  CHECK(gotrange.m_end == exprange.m_end);
  CHECK(gotrange.m_length == exprange.m_length);
}

TEST_CASE("content_range from string - invalid", "[AWS][slice][utility]")
{
  std::vector<std::string> const badstrings = {
    "bytes=1024-1692",              // malformed
    "bytes=1023-1048575/307232768", // malformed
    "bytes 1023-1022/5000",         // zero size
    "bytes -40-12/50",              // negative start
    "bytes 5-13/11"                 // past end
  };

  ContentRange cr;

  for (std::string const &badstr : badstrings) {
    if (!cr.fromStringClosed(badstr.c_str())) {
      CHECK_FALSE(cr.isValid());
      INFO(badstr.c_str());
    }
  }
}
