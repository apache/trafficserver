/** @file

   Catch based unit tests for per upstream server metric publication.

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/Net.h"
#include "tsutil/Metrics.h"
#include "tscore/ink_inet.h"

namespace
{

constexpr std::string_view FQDN{"unit.test.origin"};

// Whether the published store enumerates this name. Deliberately iteration rather than lookup(),
// because enumeration is what traffic_ctl, the JSONRPC record lookup and stats_over_http walk, and
// so is what "published" means to an operator.
bool
is_published(std::string_view metric_name)
{
  for (auto &&[name, type, value] : ts::Metrics::instance()) {
    if (name == metric_name) {
      return true;
    }
  }
  return false;
}

std::string
group_metric(std::string_view stem, std::string_view addr)
{
  return std::string("proxy.process.http.per_server.").append(stem).append(".").append(FQDN).append(".").append(addr);
}

std::string
host_metric(std::string_view stem)
{
  return std::string("proxy.process.http.per_server.").append(stem).append(".").append(FQDN);
}

// One upstream connection, opened and closed, following the same path as production: HttpSM
// reserves and then drops the group into the PoolableSession, and the session releases it when the
// connection closes. Group::release() is what erases the group at a zero count, and only that makes
// the next transaction to the same upstream construct a fresh Group and re-evaluate
// metric_aggregate. TxnState::release() alone decrements without erasing.
void
open_and_close_connection(ConnectionTracker::TxnConfig const &txn, IpEndpoint const &addr)
{
  auto state = ConnectionTracker::obtain_outbound(txn, FQDN, addr);

  REQUIRE(state.is_active());
  state.reserve();

  auto group = state.drop();
  group->release();
}

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

} // namespace

TEST_CASE("ConnectionTracker aggregate metric publication", "[net][ConnectionTracker]")
{
  auto &txn = test_config();

  txn.metric_enabled = 1;
  txn.server_match   = ConnectionTracker::MATCH_BOTH;

  IpEndpoint addr;
  REQUIRE(ats_ip_pton("10.9.8.7:443", &addr) == 0);

  const std::string current_group = group_metric("current_connection", "10.9.8.7:443");
  const std::string total_group   = group_metric("total_connection", "10.9.8.7:443");
  const std::string blocked_group = group_metric("blocked_connection", "10.9.8.7:443");

  SECTION("AGGREGATE_NONE publishes the per group metrics and no aggregate")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_NONE;
    open_and_close_connection(txn, addr);

    CHECK(is_published(current_group));
    CHECK(is_published(total_group));
    CHECK(is_published(blocked_group));
    CHECK_FALSE(is_published(host_metric("current_connection.max")));
  }

  SECTION("AGGREGATE_GROUP publishes the per group metrics, the sums and the max")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_GROUP;
    open_and_close_connection(txn, addr);

    CHECK(is_published(current_group));
    CHECK(is_published(host_metric("current_connection")));
    CHECK(is_published(host_metric("total_connection")));
    CHECK(is_published(host_metric("blocked_connection")));
    CHECK(is_published(host_metric("current_connection.max")));
  }

  SECTION("AGGREGATE_MAX publishes the max and nothing else")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_MAX;
    open_and_close_connection(txn, addr);

    CHECK(is_published(host_metric("current_connection.max")));

    CHECK_FALSE(is_published(host_metric("current_connection")));
    CHECK_FALSE(is_published(host_metric("total_connection")));
    CHECK_FALSE(is_published(host_metric("blocked_connection")));
    CHECK_FALSE(is_published(current_group));
    CHECK_FALSE(is_published(total_group));
    CHECK_FALSE(is_published(blocked_group));
  }

  SECTION("AGGREGATE_SUM publishes the sums and the max, but not the per group metrics")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_SUM;
    open_and_close_connection(txn, addr);

    CHECK(is_published(host_metric("current_connection")));
    CHECK(is_published(host_metric("total_connection")));
    CHECK(is_published(host_metric("blocked_connection")));
    CHECK(is_published(host_metric("current_connection.max")));

    CHECK_FALSE(is_published(current_group));
    CHECK_FALSE(is_published(total_group));
    CHECK_FALSE(is_published(blocked_group));
  }

  SECTION("switching to AGGREGATE_MAX retracts already published per group metrics")
  {
    // The production sequence: run for a while with the per group metrics published, then change
    // the setting. Without a retraction the first set of names is published forever.
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_NONE;
    open_and_close_connection(txn, addr);
    REQUIRE(is_published(current_group));

    txn.metric_aggregate = ConnectionTracker::AGGREGATE_MAX;
    open_and_close_connection(txn, addr);

    CHECK_FALSE(is_published(current_group));
    CHECK_FALSE(is_published(total_group));
    CHECK_FALSE(is_published(blocked_group));
    CHECK(is_published(host_metric("current_connection.max")));
  }

  SECTION("switching from AGGREGATE_SUM to AGGREGATE_MAX retracts the sums")
  {
    // The sums are aggregates rather than per group names, but they are published the same way and
    // so need withdrawing the same way when the setting stops asking for them.
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_SUM;
    open_and_close_connection(txn, addr);
    REQUIRE(is_published(host_metric("current_connection")));

    txn.metric_aggregate = ConnectionTracker::AGGREGATE_MAX;
    open_and_close_connection(txn, addr);

    CHECK_FALSE(is_published(host_metric("current_connection")));
    CHECK_FALSE(is_published(host_metric("total_connection")));
    CHECK_FALSE(is_published(host_metric("blocked_connection")));
    CHECK(is_published(host_metric("current_connection.max")));
  }

  SECTION("switching from AGGREGATE_MAX to AGGREGATE_SUM republishes the sums")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_MAX;
    open_and_close_connection(txn, addr);
    REQUIRE_FALSE(is_published(host_metric("total_connection")));

    txn.metric_aggregate = ConnectionTracker::AGGREGATE_SUM;
    open_and_close_connection(txn, addr);

    CHECK(is_published(host_metric("current_connection")));
    CHECK(is_published(host_metric("total_connection")));
    CHECK(is_published(host_metric("blocked_connection")));
  }

  SECTION("switching back to AGGREGATE_GROUP republishes the per group metrics")
  {
    txn.metric_aggregate = ConnectionTracker::AGGREGATE_MAX;
    open_and_close_connection(txn, addr);
    REQUIRE_FALSE(is_published(current_group));

    txn.metric_aggregate = ConnectionTracker::AGGREGATE_GROUP;
    open_and_close_connection(txn, addr);

    CHECK(is_published(current_group));
    CHECK(is_published(host_metric("current_connection")));
  }

  SECTION("a group with no aggregate keeps its own metrics whatever the setting")
  {
    // Only MATCH_BOTH yields a hostname to gather under, so a MATCH_PORT group has no aggregate.
    // Suppressing it would report nothing at all for that upstream.
    txn.server_match = ConnectionTracker::MATCH_PORT;

    IpEndpoint port_addr;
    REQUIRE(ats_ip_pton("10.9.8.6:80", &port_addr) == 0);

    for (auto level : {ConnectionTracker::AGGREGATE_MAX, ConnectionTracker::AGGREGATE_SUM}) {
      txn.metric_aggregate = level;
      open_and_close_connection(txn, port_addr);
      CHECK(is_published("proxy.process.http.per_server.current_connection.10.9.8.6:80"));
    }
  }
}
