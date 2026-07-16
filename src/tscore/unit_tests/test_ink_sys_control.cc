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
