/** @file

  Unit tests for HostEnt allocator lifecycle.

  Exercises the same dnsBufAllocator that DNS.cc uses in production. With the
  std::vector<SRV> in HostEnt::srv_hosts.hosts, any path that returns a HostEnt
  to the freelist without running ~vector<SRV>() leaks the vector's heap
  storage. Run under ASan/LSan: the leak shows up as a sanitizer report and
  fails the test.

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

#include <catch2/catch_test_macros.hpp>

#include "iocore/dns/DNSProcessor.h"
#include "iocore/dns/SRV.h"
#include "tscore/Allocator.h"

extern ClassAllocator<HostEnt> dnsBufAllocator;

namespace
{
void
fill_srv_hosts(HostEnt *e, int count)
{
  for (int i = 0; i < count; ++i) {
    SRV srv{};
    srv.priority = static_cast<unsigned int>(i);
    srv.host_len = 1;
    e->srv_hosts.hosts.push_back(srv);
    e->srv_hosts.srv_hosts_length += srv.host_len;
  }
}
} // namespace

TEST_CASE("HostEnt SRV vector is released when HostEnt is freed", "[dns][hostent]")
{
  SECTION("single allocation, push then free")
  {
    HostEnt *e = dnsBufAllocator.alloc();
    REQUIRE(e != nullptr);
    REQUIRE(e->srv_hosts.hosts.empty());
    REQUIRE(e->srv_hosts.srv_hosts_length == 0);

    fill_srv_hosts(e, 16);
    REQUIRE(e->srv_hosts.hosts.size() == 16);
    REQUIRE(e->srv_hosts.hosts.capacity() >= 16);

    e->free();
    // LSan flags any leaked vector<SRV> heap from the slot above.
  }

  SECTION("repeated alloc/fill/free cycle exercises freelist reuse")
  {
    for (int iter = 0; iter < 8; ++iter) {
      HostEnt *e = dnsBufAllocator.alloc();
      REQUIRE(e != nullptr);
      REQUIRE(e->srv_hosts.hosts.empty());
      REQUIRE(e->srv_hosts.srv_hosts_length == 0);

      fill_srv_hosts(e, 32);
      REQUIRE(e->srv_hosts.hosts.size() == 32);

      e->free();
    }
  }

  SECTION("multiple live allocations freed in reverse order")
  {
    constexpr int          kCount = 4;
    std::vector<HostEnt *> live;
    live.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
      HostEnt *e = dnsBufAllocator.alloc();
      REQUIRE(e != nullptr);
      fill_srv_hosts(e, 8 + i);
      live.push_back(e);
    }

    for (auto it = live.rbegin(); it != live.rend(); ++it) {
      (*it)->free();
    }
  }
}
