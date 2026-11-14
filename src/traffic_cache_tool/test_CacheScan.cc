/** @file

  Cache inspection tool alternate scanning tests.

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

#include "CacheScan.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("cache inspection rejects an alternate without a request URL", "[cache_tool]")
{
  // A damaged or partly written document can carry a request header whose URL
  // pointer is null once unmarshalled.
  char                buf[256] = {};
  swoc::MemSpan<char> mem(buf, sizeof(buf));
  ct::CacheScan       scan(nullptr);

  CHECK_FALSE(scan.check_url(mem, nullptr));
}
