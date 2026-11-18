/** @file

  Unit tests for the stripe layout traffic_cache_tool computes.

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

extern int cache_config_min_average_object_size;

TEST_CASE("A stripe with room for fewer objects than a bucket holds still gets a directory", "[cache_tool]")
{
  ct::Span     span(swoc::file::path("/dev/null"));
  ct::StripeSM stripe(&span, Bytes(0), CacheStoreBlocks(CacheStripeBlocks::SCALE / CacheStoreBlocks::SCALE));

  // A 128 MB stripe holds three 40 MB objects, short of the four entries in one directory bucket.
  cache_config_min_average_object_size = 40'000'000;
  stripe.vol_init_data();
  CHECK(stripe._segments == 1);
  CHECK(stripe._buckets == 1);

  cache_config_min_average_object_size = ESTIMATED_OBJECT_SIZE;
}
