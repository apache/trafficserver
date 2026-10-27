/** @file

    test ink_sys_control.h - system resource helpers

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

#include <tscore/ink_sys_control.h>
#include <catch2/catch_test_macros.hpp>

#include <limits>

#if defined(__linux__) || defined(darwin) || defined(freebsd)
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#endif

TEST_CASE("ink_get_current_rss reports a plausible current RSS", "[ink_sys_control]")
{
#if defined(__linux__) || defined(darwin) || defined(freebsd)
  uint64_t rss = ink_get_current_rss();

  // The test process is obviously resident, so current RSS must be non-zero
  // and well above a trivially small floor (at least one page).
  REQUIRE(rss > 0);
  REQUIRE(rss >= 4096);
  REQUIRE(rss <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));

  SECTION("RSS grows when we touch a large fresh mapping")
  {
    uint64_t before = ink_get_current_rss();

    // Map ~64 MiB of fresh anonymous memory. Unlike a heap allocation, these
    // pages are guaranteed not to have been resident before, so touching every
    // page deterministically increases RSS.
    constexpr size_t bytes   = 64 * 1024 * 1024;
    void            *mapping = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(mapping != MAP_FAILED);

    long page_size = sysconf(_SC_PAGESIZE);
    REQUIRE(page_size > 0);

    volatile char *buf = static_cast<volatile char *>(mapping);
    for (size_t i = 0; i < bytes; i += static_cast<size_t>(page_size)) {
      buf[i] = static_cast<char>(i);
    }

    uint64_t after = ink_get_current_rss();
    REQUIRE(after > before);

    munmap(mapping, bytes);
  }
#else
  // On unsupported platforms the helper is documented to return 0.
  REQUIRE(ink_get_current_rss() == 0);
#endif
}

TEST_CASE("ink_rss_pages_to_bytes converts plausible page counts", "[ink_sys_control]")
{
  CHECK(ink_rss_pages_to_bytes(0, 4096) == 0);
  CHECK(ink_rss_pages_to_bytes(1, 4096) == 4096);
  CHECK(ink_rss_pages_to_bytes(262144, 4096) == 1073741824);
  CHECK(ink_rss_pages_to_bytes(16384, 65536) == 1073741824);
}

TEST_CASE("ink_rss_pages_to_bytes reports an impossible RSS as 0", "[ink_sys_control]")
{
  constexpr uint64_t max_rss    = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  constexpr uint64_t max_uint64 = std::numeric_limits<uint64_t>::max();

  SECTION("a page size of 0")
  {
    CHECK(ink_rss_pages_to_bytes(1, 0) == 0);
    CHECK(ink_rss_pages_to_bytes(max_uint64, 0) == 0);
  }

  SECTION("the largest count that fits in an int64, and one page more")
  {
    for (uint64_t page_size : {uint64_t{1}, uint64_t{3}, uint64_t{4096}, uint64_t{16384}, uint64_t{65536}}) {
      INFO("page_size " << page_size);
      uint64_t const fits = max_rss / page_size;

      CHECK(ink_rss_pages_to_bytes(fits, page_size) == fits * page_size);
      CHECK(ink_rss_pages_to_bytes(fits + 1, page_size) == 0);
    }
  }

  SECTION("counts whose byte value would wrap")
  {
    // These wrap to a small value, which only a check made before the multiply rejects.
    CHECK(ink_rss_pages_to_bytes((uint64_t{1} << 52) + 1, 4096) == 0);
    CHECK(ink_rss_pages_to_bytes((uint64_t{1} << 63) + 1, 2) == 0);
    CHECK(ink_rss_pages_to_bytes(max_uint64, max_uint64) == 0);

    CHECK(ink_rss_pages_to_bytes(max_uint64, 4096) == 0);
    CHECK(ink_rss_pages_to_bytes(1, max_uint64) == 0);
  }
}
