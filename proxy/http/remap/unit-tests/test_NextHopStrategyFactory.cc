/** @file

  Unit tests for the NextHopStrategyFactory.

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

  Unit testing the NextHopStrategy factory.

 */

#define CATCH_CONFIG_MAIN /* include main function */
#include <catch.hpp>      /* catch unit-test framework */
#include <fstream>        /* ofstream */
#include <memory>
#include <utime.h>
#include <yaml-cpp/yaml.h>

#include "nexthop_test_stubs.h"
#include "NextHopSelectionStrategy.h"
#include "NextHopStrategyFactory.h"
#include "NextHopConsistentHash.h"
#include "NextHopRoundRobin.h"

SCENARIO("factory tests loading yaml configs", "[loadConfig]")
{
  GIVEN("Loading the strategy.yaml with included 'hosts.yaml'.")
  {
#ifdef TS_SRC_DIR
    REQUIRE(chdir(TS_SRC_DIR) == 0);
#endif
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/strategy.yaml");

    WHEN("the two files are loaded.")
    {
      THEN("there are two strategies defined in the config, 'strategy-1' and 'strategy-2'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("strategy-1") != nullptr);
        REQUIRE(nhf.strategyInstance("strategy-2") != nullptr);
        REQUIRE(nhf.strategyInstance("notthere") == nullptr);
      }
    }

    WHEN("'strategy-1' details are checked.")
    {
      THEN("Expect that these results for 'strategy-1'")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("strategy-1");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == true);
        CHECK(strategy->max_simple_retries == 1);
        CHECK(strategy->policy_type == NH_CONSISTENT_HASH);

        // down cast here using the stored pointer so that I can verify the hash_key was set
        // properly.
        NextHopConsistentHash *ptr = static_cast<NextHopConsistentHash *>(strategy.get());
        REQUIRE(ptr != nullptr);
        CHECK(ptr->hash_key == NH_CACHE_HASH_KEY);

        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                CHECK(h->available == true);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 1.5);
                CHECK(h->available == true);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("'strategy-2' details are checked.")
    {
      THEN("Expect that these results for 'strategy-2'")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("strategy-2");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_STRICT);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 1.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 1.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }

  GIVEN("loading a yaml config, simple-strategy.yaml ")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/simple-strategy.yaml");

    WHEN("loading the single file")
    {
      THEN("loading the simple-strategy.yaml")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("strategy-3") != nullptr);
        REQUIRE(nhf.strategyInstance("strategy-4") != nullptr);
      }
    }

    WHEN("'strategy-3' details are checked.")
    {
      THEN("Expect that these results for 'strategy-3'")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("strategy-3");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTPS);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.2:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("'strategy-4' details are checked.")
    {
      THEN("Expect that these results for 'strategy-4'")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("strategy-4");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_LATCHED);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_ALTERNATE_RING);
        CHECK(strategy->groups == 1);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:443");
                CHECK(h->weight == 1.0);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }

  GIVEN("loading a yaml config combining hosts and strategies into one file, combined.yaml")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/combined.yaml");

    WHEN("loading the single file")
    {
      THEN("loading the single file when there is only one strategy, 'mid-tier-east'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("mid-tier-east") != nullptr);
        REQUIRE(nhf.strategyInstance("notthere") == nullptr);
      }
    }

    WHEN("the strategy 'mid-tier-north' details are checked.")
    {
      THEN("expect the following details.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-north");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->policy_type == NH_RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(402));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(403));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-south' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-south");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_LATCHED);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-east' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-east");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_FIRST_LIVE);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == true);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NH_SCHEME_HTTPS);
        CHECK(strategy->ring_mode == NH_ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == false);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-west' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-west");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_STRICT);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTPS);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == false);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }

    WHEN("the strategy 'mid-tier-midwest' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-midwest");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_CONSISTENT_HASH);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);

        // I need to down cast here using the stored pointer so that I can verify that
        // the hash_key was set properly.
        NextHopConsistentHash *ptr = static_cast<NextHopConsistentHash *>(strategy.get());
        REQUIRE(ptr != nullptr);
        CHECK(ptr->hash_key == NH_CACHE_HASH_KEY);

        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTPS);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == false);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "s1.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.1:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.1:8443");
                CHECK(h->weight == 2.0);
                break;
              case 1:
                CHECK(h->hostname == "s2.bar.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.2.2:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.2.2:8443");
                CHECK(h->weight == 1.0);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }
}

SCENARIO("factory tests loading yaml configs from a directory", "[loadConfig]")
{
  GIVEN("Loading the strategies using a directory of 'yaml' files")
  {
    NextHopStrategyFactory nhf(TS_SRC_DIR "unit-tests/strategies-dir");

    WHEN("the two files are loaded.")
    {
      THEN("there are two strategies defined in the config, 'strategy-1' and 'strategy-2'")
      {
        REQUIRE(nhf.strategies_loaded == true);
        REQUIRE(nhf.strategyInstance("mid-tier-north") != nullptr);
        REQUIRE(nhf.strategyInstance("mid-tier-south") != nullptr);
      }
    }

    WHEN("the strategy 'mid-tier-north' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-north");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->policy_type == NH_RR_IP);
        CHECK(strategy->go_direct == true);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_EXHAUST_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(!strategy->markdown_codes.contains(405));
        CHECK(!strategy->markdown_codes.contains(502));
        CHECK(!strategy->markdown_codes.contains(503));
      }
    }

    WHEN("the strategy 'mid-tier-south' details are checked.")
    {
      THEN("expect the following results.")
      {
        std::shared_ptr<NextHopSelectionStrategy> strategy = nhf.strategyInstance("mid-tier-south");
        REQUIRE(strategy != nullptr);
        CHECK(strategy->policy_type == NH_RR_LATCHED);
        CHECK(strategy->parent_is_proxy == false);
        CHECK(strategy->ignore_self_detect == false);
        CHECK(strategy->max_simple_retries == 2);
        CHECK(strategy->go_direct == false);
        CHECK(strategy->scheme == NH_SCHEME_HTTP);
        CHECK(strategy->ring_mode == NH_ALTERNATE_RING);
        CHECK(strategy->groups == 2);
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(502));
        CHECK(!strategy->resp_codes.contains(604));
        CHECK(strategy->health_checks.active == true);
        CHECK(strategy->health_checks.passive == true);
        std::shared_ptr<HostRecord> h = strategy->host_groups[0][0];
        CHECK(h != nullptr);
        for (unsigned int i = 0; i < strategy->groups; i++) {
          CHECK(strategy->host_groups[i].size() == 2);
          for (unsigned int j = 0; j < strategy->host_groups[i].size(); j++) {
            h = strategy->host_groups[i][j];
            switch (i) {
            case 0:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p1.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.1:80");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.1:443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p2.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 80);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.2:80");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            case 1:
              switch (j) {
              case 0:
                CHECK(h->hostname == "p3.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.3:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.3:8443");
                CHECK(h->weight == 0.5);
                break;
              case 1:
                CHECK(h->hostname == "p4.foo.com");
                CHECK(h->protocols[0]->scheme == NH_SCHEME_HTTP);
                CHECK(h->protocols[0]->port == 8080);
                CHECK(h->protocols[0]->health_check_url == "http://192.168.1.4:8080");
                CHECK(h->protocols[1]->scheme == NH_SCHEME_HTTPS);
                CHECK(h->protocols[1]->port == 8443);
                CHECK(h->protocols[1]->health_check_url == "https://192.168.1.4:8443");
                CHECK(h->weight == 0.5);
                break;
              }
              break;
            }
          }
        }
        CHECK(strategy->resp_codes.contains(404));
        CHECK(strategy->resp_codes.contains(503));
        CHECK(!strategy->resp_codes.contains(604));
      }
    }
  }
}
