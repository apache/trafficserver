/** @file test_cache.cc

  Unit tests for metadata cache

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

#include <optional>
#include <random>
#include <sstream>
#include <thread>
#define CATCH_CONFIG_MAIN /* include main function */
#include "catch.hpp"      /* catch unit-test framework */
#include "../cache.h"

using namespace std::string_view_literals;
TEST_CASE("cache miss", "[slice][metadatacache]")
{
  ObjectSizeCache cache{1024};
  std::optional res = cache.get("example.com"sv);
  CHECK(res == std::nullopt);
}

TEST_CASE("cache hit", "[slice][metadatacache]")
{
  ObjectSizeCache cache{1024};
  cache.set("example.com/123"sv, 123);
  std::optional res2 = cache.get("example.com/123"sv);
  CHECK(res2.value() == 123);
}

TEST_CASE("eviction", "[slice][metadatacache]")
{
  constexpr int cache_size = 10;
  ObjectSizeCache cache{cache_size};
  for (uint64_t i = 0; i < cache_size * 100; i++) {
    std::stringstream ss;
    ss << "http://example.com/" << i;
    cache.set(ss.str(), i);
  }
  size_t found = 0;
  for (uint64_t i = 0; i < cache_size * 100; i++) {
    std::stringstream ss;
    ss << "http://example.com/" << i;
    std::optional<uint64_t> size = cache.get(ss.str());
    if (size.has_value()) {
      CHECK(size.value() == i);
      found++;
    }
  }
  REQUIRE(found == cache_size);
}

TEST_CASE("tiny cache", "[slice][metadatacache]")
{
  constexpr int cache_size = 1;
  ObjectSizeCache cache{cache_size};
  for (uint64_t i = 0; i < cache_size * 100; i++) {
    std::stringstream ss;
    ss << "http://example.com/" << i;
    cache.set(ss.str(), i);
  }
  size_t found = 0;
  for (uint64_t i = 0; i < cache_size * 100; i++) {
    std::stringstream ss;
    ss << "http://example.com/" << i;
    std::optional<uint64_t> size = cache.get(ss.str());
    if (size.has_value()) {
      CHECK(size.value() == i);
      found++;
    }
  }
  REQUIRE(found == cache_size);
}

TEST_CASE("hit rate", "[slice][metadatacache]")
{
  constexpr int cache_size = 10;
  ObjectSizeCache cache{cache_size};

  std::mt19937 gen;
  std::poisson_distribution<uint64_t> d{cache_size};

  for (uint64_t i = 0; i < cache_size * 100; i++) {
    std::stringstream ss;
    uint64_t obj = d(gen);

    ss << "http://example.com/" << obj;
    std::optional<uint64_t> size = cache.get(ss.str());
    if (size.has_value()) {
      CHECK(size.value() == obj);
    } else {
      cache.set(ss.str(), obj);
    }
  }

  auto [hits, misses, write_hits, write_misses] = cache.cache_stats();
  INFO("Hits: " << hits);
  INFO("Misses: " << misses);
  REQUIRE(hits > cache_size * 50);
}

TEST_CASE("threads", "[slice][metadatacache]")
{
  constexpr int cache_size = 10;
  ObjectSizeCache cache{cache_size};

  std::mt19937 gen;
  std::poisson_distribution<uint64_t> d{cache_size};
  std::vector<std::thread> threads;

  auto runfunc = [&]() {
    for (uint64_t i = 0; i < cache_size * 100; i++) {
      std::stringstream ss;
      uint64_t obj = d(gen);

      ss << "http://example.com/" << obj;
      std::optional<uint64_t> size = cache.get(ss.str());
      if (size.has_value()) {
        CHECK(size.value() == obj);
      } else {
        cache.set(ss.str(), obj);
      }
    }
  };

  for (int i = 0; i < 4; i++) {
    threads.emplace_back(runfunc);
  }

  for (auto &t : threads) {
    t.join();
  }
  auto [hits, misses, write_hits, write_misses] = cache.cache_stats();
  INFO("Hits: " << hits);
  INFO("Misses: " << misses);
  REQUIRE(hits > cache_size * 50 * 4);
}
