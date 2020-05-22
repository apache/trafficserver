/** @file

  Unit tests for the NextHopConsistentHash.

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

  Unit testing the NextHopConsistentHash class.

 */

#define CATCH_CONFIG_MAIN /* include main function */

#include <catch.hpp> /* catch unit-test framework */
#include <yaml-cpp/yaml.h>

#include "nexthop_test_stubs.h"
#include "NextHopSelectionStrategy.h"
#include "NextHopStrategyFactory.h"
#include "NextHopConsistentHash.h"

#include "HTTP.h"
extern int cmd_disable_pfreelist;

SCENARIO("Testing NextHopConsistentHash class, using policy 'consistent_hash'", "[NextHopConsistentHash]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the consistent-hash-tests.yaml config for 'consistent_hash' tests.")
  {
    // load the configuration strtegies.
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/consistent-hash-tests.yaml");
    strategy = nhf.strategyInstance("consistent-hash-1");

    WHEN("the config is loaded.")
    {
      THEN("then testing consistent hash.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->groups == 3);
      }
    }

    WHEN("requests are received.")
    {
      HttpRequestData request;
      ParentResult result;
      TestData rdata;
      rdata.xact_start        = time(nullptr);
      uint64_t fail_threshold = 1;
      uint64_t retry_time     = 1;

      // need to run these checks in succession so there
      // are no host status state changes.
      //
      // These tests simulate failed requests using a selected host.
      // markNextHopDown() is called by the state machine when
      // there is a request failure due to a connection error or
      // timeout.  the 'result' struct has the information on the
      // host used in the failed request and when called, marks the
      // host indicated in the 'request' struct as unavailable.
      //
      // Here we walk through making requests then marking the selected
      // host down until all are down and the origin is finally chosen.
      //
      THEN("when making requests and taking nodes down.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request.
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(10001, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "p1.foo.com") == 0);

        // mark down p1.foo.com.  markNextHopDown looks at the 'result'
        // and uses the host index there mark down the host selected
        // from a
        strategy->markNextHopDown(10001, result, 1, fail_threshold);

        // second request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10002, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "p2.foo.com") == 0);

        // mark down p2.foo.com
        strategy->markNextHopDown(10002, result, 1, fail_threshold);

        // third request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10003, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "s2.bar.com") == 0);

        // mark down s2.bar.com
        strategy->markNextHopDown(10003, result, 1, fail_threshold);

        // fourth request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10004, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "s1.bar.com") == 0);

        // mark down s1.bar.com.
        strategy->markNextHopDown(10004, result, 1, fail_threshold);

        // fifth request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10005, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "q1.bar.com") == 0);

        // mark down q1.bar.com
        strategy->markNextHopDown(10005, result, 1, fail_threshold);
        // sixth request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10006, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "q2.bar.com") == 0);

        // mark down q2.bar.com
        strategy->markNextHopDown(10006, result, 1, fail_threshold);
        // seventh request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10007, result, request, fail_threshold, retry_time);

        CHECK(result.result == ParentResultType::PARENT_DIRECT);
        CHECK(result.hostname == nullptr);

        // sleep and test that q2 is becomes retryable;
        time_t now = time(nullptr) + 5;

        // eighth request - reusing the ParentResult from the last request
        // simulating a failure triggers a search for another parent, not firstcall.
        build_request(request, "rabbit.net");
        strategy->findNextHop(10008, result, request, fail_threshold, retry_time, now);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "q2.bar.com") == 0);
      }
      // free up request resources.
      br_destroy(request);
    }
  }
}

SCENARIO("Testing NextHopConsistentHash class (all firstcalls), using policy 'consistent_hash'", "[NextHopConsistentHash]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the consistent-hash-tests.yaml config for 'consistent_hash' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/consistent-hash-tests.yaml");
    strategy = nhf.strategyInstance("consistent-hash-1");

    WHEN("the config is loaded.")
    {
      THEN("then testing consistent hash.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->groups == 3);
      }
    }

    // Same test procedure as the first scenario but we clear the 'result' struct
    // so that we are making initial requests and simulating that hosts were
    // removed by different transactions.
    //
    // these checks need to be run in sequence so that there are no host status
    // state changes induced by using multiple WHEN() and THEN()
    WHEN("initial requests are made and hosts are unavailable .")
    {
      uint64_t fail_threshold = 1;
      uint64_t retry_time     = 1;
      TestData rdata;
      rdata.xact_start = time(nullptr);
      HttpRequestData request;
      ParentResult result;

      THEN("when making requests and taking nodes down.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request.
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(20001, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "p1.foo.com") == 0);

        // mark down p1.foo.com
        strategy->markNextHopDown(20001, result, 1, fail_threshold);
        // second request
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(20002, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "p2.foo.com") == 0);

        // mark down p2.foo.com
        strategy->markNextHopDown(20002, result, 1, fail_threshold);

        // third request
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(20003, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "s2.bar.com") == 0);

        // mark down s2.bar.com
        strategy->markNextHopDown(20003, result, 1, fail_threshold);

        // fourth request
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(20004, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "s1.bar.com") == 0);

        // mark down s1.bar.com
        strategy->markNextHopDown(20004, result, 1, fail_threshold);

        // fifth request
        build_request(request, "rabbit.net/asset1");
        result.reset();
        strategy->findNextHop(20005, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "q1.bar.com") == 0);

        // sixth request - wait and p1 should now become available
        time_t now = time(nullptr) + 5;
        build_request(request, "rabbit.net");
        result.reset();
        strategy->findNextHop(20006, result, request, fail_threshold, retry_time, now);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "p1.foo.com") == 0);
      }
      // free up request resources.
      br_destroy(request);
    }
  }
}

SCENARIO("Testing NextHopConsistentHash class (alternating rings), using policy 'consistent_hash'", "[NextHopConsistentHash]")
{
  // We need this to build a HdrHeap object in build_request();
  // No thread setup, forbid use of thread local allocators.
  cmd_disable_pfreelist = true;
  // Get all of the HTTP WKS items populated.
  http_init();

  GIVEN("Loading the consistent-hash-tests.yaml config for 'consistent_hash' tests.")
  {
    std::shared_ptr<NextHopSelectionStrategy> strategy;
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/consistent-hash-tests.yaml");
    strategy = nhf.strategyInstance("consistent-hash-2");

    WHEN("the config is loaded.")
    {
      THEN("then testing consistent hash.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);
        REQUIRE(strategy->groups == 3);
      }
    }

    // makeing requests and marking down hosts with a config set for alternating ring mode.
    WHEN("requests are made in a config set for alternating rings and hosts are marked down.")
    {
      uint64_t fail_threshold = 1;
      uint64_t retry_time     = 1;
      TestData rdata;
      rdata.xact_start = time(nullptr);
      HttpRequestData request;
      ParentResult result;

      THEN("expect the following results when making requests and marking hosts down.")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(strategy != nullptr);

        // first request.
        build_request(request, "bunny.net/asset1");
        result.reset();
        strategy->findNextHop(30001, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c2.foo.com") == 0);

        // simulated failure, mark c2 down and retry request
        strategy->markNextHopDown(30001, result, 1, fail_threshold);

        // second request
        build_request(request, "bunny.net.net/asset1");
        strategy->findNextHop(30002, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c3.bar.com") == 0);

        // mark down c3.bar.com
        strategy->markNextHopDown(30002, result, 1, fail_threshold);

        // third request
        build_request(request, "bunny.net/asset2");
        result.reset();
        strategy->findNextHop(30003, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c6.bar.com") == 0);

        // just mark it down and retry request
        strategy->markNextHopDown(30003, result, 1, fail_threshold);
        // fourth request
        build_request(request, "bunny.net/asset2");
        strategy->findNextHop(30004, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c1.foo.com") == 0);

        // mark it down
        strategy->markNextHopDown(30004, result, 1, fail_threshold);
        // fifth request - new request
        build_request(request, "bunny.net/asset3");
        result.reset();
        strategy->findNextHop(30005, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c4.bar.com") == 0);

        // mark it down and retry
        strategy->markNextHopDown(30005, result, 1, fail_threshold);
        // sixth request
        build_request(request, "bunny.net/asset3");
        result.reset();
        strategy->findNextHop(30006, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c5.bar.com") == 0);

        // mark it down
        strategy->markNextHopDown(30006, result, 1, fail_threshold);
        // seventh request - new request with all hosts down and go_direct is false.
        build_request(request, "bunny.net/asset4");
        result.reset();
        strategy->findNextHop(30007, result, request, fail_threshold, retry_time);
        CHECK(result.result == ParentResultType::PARENT_FAIL);
        CHECK(result.hostname == nullptr);

        // eighth request - retry after waiting for the retry window to expire.
        time_t now = time(nullptr) + 5;
        build_request(request, "bunny.net/asset4");
        result.reset();
        strategy->findNextHop(30008, result, request, fail_threshold, retry_time, now);
        CHECK(result.result == ParentResultType::PARENT_SPECIFIED);
        CHECK(strcmp(result.hostname, "c2.foo.com") == 0);
      }
      // free up request resources.
      br_destroy(request);
    }
  }
}
