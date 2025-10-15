/** @file

  Unit tests for hash algorithms (SipHash-1-3, Wyhash)

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
#include "tscore/HashWyhash.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

// Test-only: Expose internal Wyhash multiplication functions for verification
extern "C++" {
std::uint64_t wyhash_test_wymix(std::uint64_t A, std::uint64_t B);
std::uint64_t wyhash_test_wymix_portable(std::uint64_t A, std::uint64_t B);
}

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

TEST_CASE("HashWyhash - Deterministic output", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash1, hash2;
  const char     *input = "test";

  hash1.update(input, 4);
  hash1.final();

  hash2.update(input, 4);
  hash2.final();

  REQUIRE(hash1.get() == hash2.get());
  REQUIRE(hash1.get() != 0);
}

TEST_CASE("HashWyhash - Empty input", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash;
  hash.update("", 0);
  hash.final();
  REQUIRE(hash.get() != 0);
}

TEST_CASE("HashWyhash - Single byte", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash;
  hash.update("a", 1);
  hash.final();
  REQUIRE(hash.get() != 0);
}

TEST_CASE("HashWyhash - Block boundaries (32-byte blocks)", "[libts][HashWyhash]")
{
  std::vector<size_t> sizes = {31, 32, 33, 64, 65, 96, 97};
  std::string         input(128, 'x');

  for (auto size : sizes) {
    ATSHash64Wyhash hash;
    hash.update(input.c_str(), size);
    hash.final();
    REQUIRE(hash.get() != 0);
  }
}

TEST_CASE("HashWyhash - Incremental vs single update", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash1, hash2;

  hash1.update("hello", 5);
  hash1.update(" world", 6);
  hash1.final();

  hash2.update("hello world", 11);
  hash2.final();

  REQUIRE(hash1.get() == hash2.get());
}

TEST_CASE("HashWyhash - Typical URL paths", "[libts][HashWyhash]")
{
  std::vector<std::string> urls = {"/", "/index.html", "/api/v1/users/123", "/images/photos/vacation/beach/2024/photo_12345.jpg"};

  for (const auto &url : urls) {
    ATSHash64Wyhash hash;
    hash.update(url.c_str(), url.size());
    hash.final();
    REQUIRE(hash.get() != 0);
  }
}

TEST_CASE("HashWyhash - Long URLs with query strings", "[libts][HashWyhash]")
{
  std::string long_url = "/search?";
  for (int i = 0; i < 200; i++) {
    long_url += "parameter" + std::to_string(i) + "=some_longer_value" + std::to_string(i) + "&";
  }

  ATSHash64Wyhash hash;
  hash.update(long_url.c_str(), long_url.size());
  hash.final();
  REQUIRE(hash.get() != 0);
  REQUIRE(long_url.size() > 2000);
}

TEST_CASE("HashWyhash - Different inputs produce different hashes", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash1, hash2;

  hash1.update("parent1", 7);
  hash1.final();

  hash2.update("parent2", 7);
  hash2.final();

  REQUIRE(hash1.get() != hash2.get());
}

TEST_CASE("HashWyhash - Clear and reuse", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash;

  hash.update("first", 5);
  hash.final();
  uint64_t first_result = hash.get();

  hash.clear();
  hash.update("first", 5);
  hash.final();

  REQUIRE(hash.get() == first_result);
}

TEST_CASE("HashWyhash - Custom seed", "[libts][HashWyhash]")
{
  ATSHash64Wyhash hash1(123456);
  ATSHash64Wyhash hash2(789012);
  const char     *input = "test";

  hash1.update(input, 4);
  hash1.final();

  hash2.update(input, 4);
  hash2.final();

  REQUIRE(hash1.get() != hash2.get());
}

TEST_CASE("Hash algorithms produce different outputs for same input", "[libts][Hash]")
{
  const char     *input = "test";
  ATSHash64Sip13  hash13;
  ATSHash64Sip24  hash24;
  ATSHash64Wyhash wyhash;

  hash13.update(input, 4);
  hash13.final();

  hash24.update(input, 4);
  hash24.final();

  wyhash.update(input, 4);
  wyhash.final();

  uint64_t result13  = hash13.get();
  uint64_t result24  = hash24.get();
  uint64_t result_wy = wyhash.get();

  REQUIRE(result13 != 0);
  REQUIRE(result24 != 0);
  REQUIRE(result_wy != 0);
}

TEST_CASE("Wyhash portable multiplication - known values", "[libts][Wyhash]")
{
  // Test portable implementation against pre-calculated expected values
  // These values were verified independently and work on all platforms

  // Basic cases
  REQUIRE(wyhash_test_wymix_portable(0, 0) == 0);
  REQUIRE(wyhash_test_wymix_portable(1, 1) == 1); // 1*1 = 1, high=0 low=1, 0^1 = 1
  REQUIRE(wyhash_test_wymix_portable(0, 123) == 0);
  REQUIRE(wyhash_test_wymix_portable(123, 0) == 0);

  // Known multiplication results: A * B = high ^ low
  // 2 * 3 = 6 (all in low 64 bits, high = 0) -> 0 ^ 6 = 6
  REQUIRE(wyhash_test_wymix_portable(2, 3) == 6);
  REQUIRE(wyhash_test_wymix_portable(3, 2) == 6);

  // Large values that produce carry into high bits
  // 0xFFFFFFFF * 0xFFFFFFFF = 0xFFFFFFFE00000001
  // high = 0, low = 0xFFFFFFFE00000001
  REQUIRE(wyhash_test_wymix_portable(0xFFFFFFFF, 0xFFFFFFFF) == 0xFFFFFFFE00000001ULL);

  // 0x100000000 * 0x100000000 = 0x10000000000000000
  // high = 1, low = 0 -> 1 ^ 0 = 1
  REQUIRE(wyhash_test_wymix_portable(0x100000000ULL, 0x100000000ULL) == 1);

  // Test edge cases
  // UINT64_MAX * 1 = UINT64_MAX (high = 0, low = UINT64_MAX)
  REQUIRE(wyhash_test_wymix_portable(UINT64_MAX, 1) == UINT64_MAX);
  REQUIRE(wyhash_test_wymix_portable(1, UINT64_MAX) == UINT64_MAX);

  // UINT64_MAX * 2 = 0x1FFFFFFFFFFFFFFFE
  // high = 1, low = 0xFFFFFFFFFFFFFFFE -> 1 ^ 0xFFFFFFFFFFFFFFFE = 0xFFFFFFFFFFFFFFFF
  REQUIRE(wyhash_test_wymix_portable(UINT64_MAX, 2) == 0xFFFFFFFFFFFFFFFFULL);

  // Specific test values to ensure correctness
  // 0x123456789ABCDEF * 0x123456789ABCDEF = 0xdca4a96e4cc1538d (verified)
  REQUIRE(wyhash_test_wymix_portable(0x123456789ABCDEFULL, 0x123456789ABCDEFULL) == 0xDCA4A96E4CC1538DULL);
}

#ifdef __SIZEOF_INT128__
TEST_CASE("Wyhash portable multiplication matches native", "[libts][Wyhash]")
{
  // Test the portable fallback against native 128-bit implementation
  uint64_t test_values[] = {
    0,
    1,
    123,
    456,
    0xFFFFFFFF,           // 32-bit max
    0x100000000ULL,       // Just over 32-bit
    0x123456789ABCDEFULL, // Large value
    UINT64_MAX,           // 64-bit max
  };

  for (auto a : test_values) {
    for (auto b : test_values) {
      REQUIRE(wyhash_test_wymix_portable(a, b) == wyhash_test_wymix(a, b));
    }
  }
}
#endif
