/** @file

  Catch based unit tests for ConnectingEntry.

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

#include "proxy/http/ConnectingEntry.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/Net.h"
#include "tscore/ink_inet.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace
{

constexpr std::string_view FQDN{"connecting.entry.test.origin"};

ConnectionTracker::TxnConfig &
test_config()
{
  // config_init keeps pointers to these for the records callbacks, so they must outlive the test.
  static ConnectionTracker::GlobalConfig global;
  static ConnectionTracker::TxnConfig    txn;
  static bool                            initialized = false;

  if (!initialized) {
    ink_net_init(NET_SYSTEM_MODULE_PUBLIC_VERSION);
    ConnectionTracker::config_init(&global, &txn, [](const char *, RecDataT, RecData, void *) -> int { return REC_ERR_OKAY; });
    initialized = true;
  }

  return txn;
}

// A reservation on the outbound group for @a addr_text, as HttpSM makes before it creates a ConnectingEntry.
std::shared_ptr<ConnectionTracker::Group>
reserved_group(char const *addr_text)
{
  IpEndpoint addr;

  REQUIRE(0 == ats_ip_pton(addr_text, &addr));

  auto state = ConnectionTracker::obtain_outbound(test_config(), FQDN, addr);

  REQUIRE(state.is_active());
  state.reserve();

  return state.drop();
}

bool
in_outbound_table(std::shared_ptr<ConnectionTracker::Group> const &group)
{
  std::vector<std::shared_ptr<ConnectionTracker::Group const>> groups;

  ConnectionTracker::get_outbound_groups(groups);
  for (auto const &g : groups) {
    if (g == group) {
      return true;
    }
  }

  return false;
}

} // namespace

// A failed or cancelled connect deletes the entry while it still holds the reservation.
TEST_CASE("ConnectingEntry destructor returns the outbound reservation", "[ConnectingEntry]")
{
  auto group = reserved_group("192.0.2.10:80");

  REQUIRE(group->_count == 1);
  REQUIRE(in_outbound_table(group));

  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking(group);
  delete entry;

  CHECK(group->_count == 0);
  CHECK_FALSE(in_outbound_table(group));
}

TEST_CASE("ConnectingEntry destructor releases exactly one reservation", "[ConnectingEntry]")
{
  auto group = reserved_group("192.0.2.11:80");
  auto other = reserved_group("192.0.2.11:80");

  REQUIRE(group == other);
  REQUIRE(group->_count == 2);

  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking(group);
  delete entry;

  CHECK(other->_count == 1);
  CHECK(in_outbound_table(other));

  other->release();
}

// With connection tracking off, HttpSM still hands the entry its (empty) group; deleting it must not touch the tracker.
TEST_CASE("ConnectingEntry destructor without connection tracking", "[ConnectingEntry]")
{
  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking({});
  delete entry;
}
