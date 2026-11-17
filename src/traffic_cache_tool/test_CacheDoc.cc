/** @file

  Cache inspection tool document length tests.

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

#include "CacheDefs.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("cache inspection checks document lengths against the bytes read", "[cache_tool]")
{
  ts::Doc       doc{};
  int64_t const read = 4096;

  doc.len  = sizeof(ts::Doc) + 100 + 200;
  doc.hlen = 100;
  CHECK(doc.fits_in(read));
  CHECK(doc.fits_in(doc.len));
  CHECK_FALSE(doc.fits_in(doc.len - 1));
  CHECK_FALSE(doc.fits_in(sizeof(ts::Doc) - 1));
  CHECK_FALSE(doc.fits_in(-1));

  doc.len = read + 1;
  CHECK_FALSE(doc.fits_in(read));

  doc.len = sizeof(ts::Doc) - 1;
  CHECK_FALSE(doc.fits_in(read));

  doc.len  = sizeof(ts::Doc) + 100;
  doc.hlen = 101;
  CHECK_FALSE(doc.fits_in(read));

  doc.hlen = UINT32_MAX;
  CHECK_FALSE(doc.fits_in(read));
}
