/** @file

  Unit tests for the NextHopRoundRobin.

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

  @section details Details

  Unit testing the NextHopRoundRobin class.

 */

#define CATCH_CONFIG_MAIN /* include main function */

#include <catch.hpp> /* catch unit-test framework */
#include <yaml-cpp/yaml.h>

#include "HttpSM.h"
#include "nexthop_test_stubs.h"
#include "NextHopSelectionStrategy.h"
#include "NextHopStrategyFactory.h"
#include "NextHopRoundRobin.h"

SCENARIO("Testing NextHopRoundRobin class, using policy 'rr-strict'", "[NextHopRoundRobin]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the round-robin-tests.yaml config for round robin 'rr-strict' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/round-robin-tests.yaml");
    strategy = nhf.strategyInstance("rr-strict-exhaust-ring");

    WHEN("the config is loaded.")
    {
      THEN("the rr-strict strategy is ready for use.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->policy_type == NH_RR_STRICT);
      }
    }

    WHEN("making requests using a 'rr-strict' policy.")
    {
      HttpSM sm;
      ParentResult *result = &sm.t_state.parent_result;
      TSHttpTxn txnp       = reinterpret_cast<TSHttpTxn>(&sm);

      THEN("then testing rr-strict.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request.
        build_request(10001, &sm, nullptr, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);

        // second request.
        build_request(10002, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p2.foo.com") == 0);

        // third request.
        build_request(10003, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);

        // did not reset result, kept it as last parent selected was p1.fo.com, mark it down and we should only select p2.foo.com
        strategy->markNextHop(txnp, result->hostname, result->port, NH_MARK_DOWN);

        // fourth request, p1 is down should select p2.
        build_request(10004, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p2.foo.com") == 0);

        // fifth request, p1 is down should still select p2.
        build_request(10005, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p2.foo.com") == 0);

        // mark down p2.
        strategy->markNextHop(txnp, result->hostname, result->port, NH_MARK_DOWN);

        // fifth request, p1 and p2 are both down, should get s1.bar.com from failover ring.
        build_request(10006, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "s1.bar.com") == 0);

        // sixth request, p1 and p2 are still down, should get s1.bar.com from failover ring.
        build_request(10007, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "s1.bar.com") == 0);

        // mark down s1.
        strategy->markNextHop(txnp, result->hostname, result->port, NH_MARK_DOWN);

        // seventh request, p1, p2, s1 are down, should get s2.bar.com from failover ring.
        build_request(10008, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "s2.bar.com") == 0);

        // mark down s2.
        strategy->markNextHop(txnp, result->hostname, result->port, NH_MARK_DOWN);

        // eighth request, p1, p2, s1, s2 are down, should get PARENT_DIRECT as go_direct is true
        build_request(10009, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(result->result == ParentResultType::PARENT_DIRECT);

        // check that nextHopExists() returns false when all parents are down.
        CHECK(strategy->nextHopExists(txnp) == false);

        // change the request time to trigger a retry.
        time_t now = (time(nullptr) + 5);

        // ninth request, p1 and p2 are still down, should get p2.foo.com as it will be retried
        build_request(10010, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp, nullptr, now);
        REQUIRE(result->result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result->hostname, "p2.foo.com") == 0);

        // tenth request, p1 should now be retried.
        build_request(10011, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp, nullptr, now);
        REQUIRE(result->result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);
      }
      br_destroy(sm);
    }
  }
}

SCENARIO("Testing NextHopRoundRobin class, using policy 'first-live'", "[NextHopRoundRobin]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the round-robin-tests.yaml config for round robin 'first-live' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/round-robin-tests.yaml");
    strategy = nhf.strategyInstance("first-live");

    WHEN("the config is loaded.")
    {
      THEN("the 'first-live' strategy is available.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->policy_type == NH_FIRST_LIVE);
      }
    }

    WHEN("when using a strategy with a 'first-live' policy.")
    {
      HttpSM sm;
      ParentResult *result = &sm.t_state.parent_result;
      TSHttpTxn txnp       = reinterpret_cast<TSHttpTxn>(&sm);

      THEN("when making requests and marking down hosts.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request.
        build_request(10012, &sm, nullptr, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);

        // second request.
        build_request(10013, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);

        // mark down p1.
        strategy->markNextHop(txnp, result->hostname, result->port, NH_MARK_DOWN);

        // third request.
        build_request(10014, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p2.foo.com") == 0);

        // change the request time to trigger a retry.
        time_t now = (time(nullptr) + 5);

        // fourth request, p1 should be marked for retry
        build_request(10015, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp, nullptr, now);
        CHECK(strcmp(result->hostname, "p1.foo.com") == 0);
      }
      br_destroy(sm);
    }
  }
}

SCENARIO("Testing NextHopRoundRobin class, using policy 'rr-ip'", "[NextHopRoundRobin]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the round-robin-tests.yaml config for round robin 'rr-ip' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/round-robin-tests.yaml");
    strategy = nhf.strategyInstance("rr-ip");
    sockaddr_in sa1, sa2;
    sa1.sin_port   = 10000;
    sa1.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.1", &(sa1.sin_addr));
    sa2.sin_port   = 10001;
    sa2.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.2", &(sa2.sin_addr));
    HttpSM sm;
    ParentResult *result = &sm.t_state.parent_result;
    TSHttpTxn txnp       = reinterpret_cast<TSHttpTxn>(&sm);

    WHEN("the config is loaded.")
    {
      THEN("then the 'rr-strict' strategy is ready.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->policy_type == NH_RR_IP);
      }
    }

    WHEN("using the 'rr-strict' strategy.")
    {
      THEN("when making requests and marking down hosts.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // call and test parentExists(), this call should not affect
        // findNextHop() round robin strict results
        CHECK(strategy->nextHopExists(txnp) == true);

        // first request.
        // memcpy(&rdata.client_ip, &sa1, sizeof(sa1));
        build_request(10016, &sm, &sa1, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p4.foo.com") == 0);

        // call and test parentExists(), this call should not affect
        // findNextHop round robin strict results.
        CHECK(strategy->nextHopExists(txnp) == true);

        // second request.
        // memcpy(&rdata.client_ip, &sa2, sizeof(sa2));
        build_request(10017, &sm, &sa2, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p3.foo.com") == 0);

        // call and test parentExists(), this call should not affect
        // findNextHop() round robin strict results
        CHECK(strategy->nextHopExists(txnp) == true);

        // third  request with same client ip, result should still be p3
        build_request(10018, &sm, &sa2, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p3.foo.com") == 0);

        // call and test parentExists(), this call should not affect
        // findNextHop() round robin strict results.
        CHECK(strategy->nextHopExists(txnp) == true);

        // fourth  request with same client ip and same result indicating a failure should result in p4
        // being selected.
        build_request(10019, &sm, &sa2, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p4.foo.com") == 0);
      }
    }
    br_destroy(sm);
  }
}

SCENARIO("Testing NextHopRoundRobin class, using policy 'latched'", "[NextHopRoundRobin]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the round-robin-tests.yaml config for round robin 'latched' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/round-robin-tests.yaml");
    strategy = nhf.strategyInstance("latched");

    WHEN("the config is loaded.")
    {
      THEN("then the 'latched' strategy is available.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->policy_type == NH_RR_LATCHED);
      }
    }

    WHEN("using a strategy having a 'latched' policy.")
    {
      HttpSM sm;
      ParentResult *result = &sm.t_state.parent_result;
      TSHttpTxn txnp       = reinterpret_cast<TSHttpTxn>(&sm);

      THEN("when making requests and marking down hosts.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request should select p3
        build_request(10020, &sm, nullptr, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p3.foo.com") == 0);

        // second request should select p3
        build_request(10021, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p3.foo.com") == 0);

        // third request, use previous result to simulate a failure, we should now select p4.
        build_request(10022, &sm, nullptr, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p4.foo.com") == 0);

        // fourth request we should be latched on p4
        build_request(10023, &sm, nullptr, "rabbit.net", nullptr);
        result->reset();
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p4.foo.com") == 0);

        // fifth request, use previous result to simulate a failure, we should now select p3.
        build_request(10024, &sm, nullptr, "rabbit.net", nullptr);
        strategy->findNextHop(txnp);
        CHECK(strcmp(result->hostname, "p3.foo.com") == 0);
      }
      br_destroy(sm);
    }
  }
}
