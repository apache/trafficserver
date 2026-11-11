/** @file

  Cache inspection tool version compatibility tests.

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

TEST_CASE("cache inspection accepts supported stripe versions", "[cache_tool]")
{
  ts::StripeMeta meta{};

  meta.magic = ts::StripeMeta::MAGIC;
  for (unsigned short minor : {1, 2, 3}) {
    meta.version = ts::VersionNumber{24, minor};
    CHECK(ct::StripeSM::validateMeta(&meta));
  }
  meta.version = ts::VersionNumber{CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION + 1};
  CHECK_FALSE(ct::StripeSM::validateMeta(&meta));
  meta.version = ts::VersionNumber{CACHE_DB_MAJOR_VERSION + 1, 0};
  CHECK_FALSE(ct::StripeSM::validateMeta(&meta));
  meta.version = CACHE_DB_VERSION;
  meta.magic   = 0;
  CHECK_FALSE(ct::StripeSM::validateMeta(&meta));
}
