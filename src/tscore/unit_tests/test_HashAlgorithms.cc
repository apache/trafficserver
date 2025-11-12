/** @file

  Unit tests for hash algorithms (SipHash-1-3, SipHash-2-4)

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

#include "tscore/HashSip.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

TEST_CASE("HashSip13 - Deterministic output", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash1, hash2;
  const char    *input = "test";

  hash1.update(input, 4);
  hash1.final();

  hash2.update(input, 4);
  hash2.final();

  REQUIRE(hash1.get() == hash2.get());
  REQUIRE(hash1.get() != 0);
}

TEST_CASE("HashSip13 - Empty input", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash;
  hash.update("", 0);
  hash.final();
  REQUIRE(hash.get() != 0);
}

TEST_CASE("HashSip13 - Single byte", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash;
  hash.update("a", 1);
  hash.final();
  REQUIRE(hash.get() != 0);
}

TEST_CASE("HashSip13 - Block boundaries", "[libts][HashSip13]")
{
  std::vector<size_t> sizes = {7, 8, 9, 16, 17, 31, 32, 33};
  std::string         input(64, 'x');

  for (auto size : sizes) {
    ATSHash64Sip13 hash;
    hash.update(input.c_str(), size);
    hash.final();
    REQUIRE(hash.get() != 0);
  }
}

TEST_CASE("HashSip13 - Incremental vs single update", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash1, hash2;

  hash1.update("hello", 5);
  hash1.update(" world", 6);
  hash1.final();

  hash2.update("hello world", 11);
  hash2.final();

  REQUIRE(hash1.get() == hash2.get());
}

TEST_CASE("HashSip13 - Typical URL paths", "[libts][HashSip13]")
{
  std::vector<std::string> urls = {"/", "/index.html", "/api/v1/users/123", "/images/photos/vacation/beach/2024/photo_12345.jpg"};

  for (const auto &url : urls) {
    ATSHash64Sip13 hash;
    hash.update(url.c_str(), url.size());
    hash.final();
    REQUIRE(hash.get() != 0);
  }
}

TEST_CASE("HashSip13 - Long URLs with query strings", "[libts][HashSip13]")
{
  std::string long_url = "/search?";
  for (int i = 0; i < 200; i++) {
    long_url += "parameter" + std::to_string(i) + "=some_longer_value" + std::to_string(i) + "&";
  }

  ATSHash64Sip13 hash;
  hash.update(long_url.c_str(), long_url.size());
  hash.final();
  REQUIRE(hash.get() != 0);
  REQUIRE(long_url.size() > 2000);
}

TEST_CASE("HashSip13 - Different inputs produce different hashes", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash1, hash2;

  hash1.update("parent1", 7);
  hash1.final();

  hash2.update("parent2", 7);
  hash2.final();

  REQUIRE(hash1.get() != hash2.get());
}

TEST_CASE("HashSip13 - Clear and reuse", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash;

  hash.update("first", 5);
  hash.final();
  uint64_t first_result = hash.get();

  hash.clear();
  hash.update("first", 5);
  hash.final();

  REQUIRE(hash.get() == first_result);
}

TEST_CASE("HashSip13 - Comparison with SipHash-2-4", "[libts][HashSip13]")
{
  ATSHash64Sip13 hash13;
  ATSHash64Sip24 hash24;
  const char    *input = "test";

  hash13.update(input, 4);
  hash13.final();

  hash24.update(input, 4);
  hash24.final();

  REQUIRE(hash13.get() != 0);
  REQUIRE(hash24.get() != 0);
}
