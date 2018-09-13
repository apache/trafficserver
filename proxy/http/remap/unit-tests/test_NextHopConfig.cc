/** @file

  A brief file description

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

#include <iostream>
#include "NextHopConfig.h"
#include "tsconfig/Errata.h"
#include "tscore/Diags.h"
#include "yaml-cpp/yaml.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("1", "[loadConfig]")
{
  INFO("Test 1, loading a non-existent file.")

  NextHopConfig nh;
  ts::Errata result;

  result = nh.loadConfig("notfound.yaml");
  INFO("messaage text: " + result.top().text());
  REQUIRE(result.top().getCode() == 1);
}

TEST_CASE("2", "[loadConfig]")
{
  INFO("Test 1, loading a strategy file with a bad include file.")

  NextHopConfig nh;
  ts::Errata result;

  result = nh.loadConfig("unit-tests/bad_include.yaml");
  INFO("messaage text: " + result.top().text());
  REQUIRE(result.top().getCode() == 1);
}

TEST_CASE("3", "[loadConfig]")
{
  INFO("Test 2, loading a good file.")

  NextHopConfig nh;
  ts::Errata result;

  result = nh.loadConfig("unit-tests/strategy.yaml");
  INFO("messaage text: " + result.top().text());
  REQUIRE(result.top().getCode() == 0);

  CHECK(nh.config.policy == CONSISTENT_HASH);
  CHECK(nh.config.hashKey == PATH_QUERY);
  CHECK(nh.config.protocol == HTTP);
  CHECK(nh.config.failover.ringMode == EXHAUST_RINGS);
  CHECK(nh.config.failover.responseCodes[0] == 404);
  CHECK(nh.config.failover.responseCodes[1] == 503);
  CHECK(nh.config.failover.healthChecks[0] == PASSIVE);

  CHECK(nh.config.groups[0][0].host == "p1-cache.foo.com");
  CHECK(nh.config.groups[0][0].healthCheckUrl == "tcp://192.168.1.1:80");
  CHECK(nh.config.groups[0][0].weight == 1.0);
  CHECK(nh.config.groups[0][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][0].protocols[0].port == 80);
  CHECK(nh.config.groups[0][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][0].protocols[1].port == 443);

  CHECK(nh.config.groups[0][1].host == "p2-cache.foo.com");
  CHECK(nh.config.groups[0][1].healthCheckUrl == "tcp://192.168.1.2:80");
  CHECK(nh.config.groups[0][1].weight == 2.0);
  CHECK(nh.config.groups[0][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[0][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][1].protocols[1].port == 8443);

  CHECK(nh.config.groups[1][0].host == "s1-cache.bar.com");
  CHECK(nh.config.groups[1][0].healthCheckUrl == "tcp://192.168.2.1:80");
  CHECK(nh.config.groups[1][0].weight == 0.1);
  CHECK(nh.config.groups[1][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][0].protocols[0].port == 80);
  CHECK(nh.config.groups[1][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][0].protocols[1].port == 443);

  CHECK(nh.config.groups[1][1].host == "s2-cache.bar.com");
  CHECK(nh.config.groups[1][1].healthCheckUrl == "tcp://192.168.2.2:80");
  CHECK(nh.config.groups[1][1].weight == 0.9);
  CHECK(nh.config.groups[1][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[1][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][1].protocols[1].port == 8443);
}

TEST_CASE("4", "[loadConfig]")
{
  INFO("Test 2, loading a combined hosts and strategy file with no #include.")

  NextHopConfig nh;
  ts::Errata result;

  result = nh.loadConfig("unit-tests/combined.yaml");
  INFO("messaage text: " + result.top().text());
  REQUIRE(result.top().getCode() == 0);

  CHECK(nh.config.policy == CONSISTENT_HASH);
  CHECK(nh.config.hashKey == PATH_QUERY);
  CHECK(nh.config.protocol == HTTP);
  CHECK(nh.config.failover.ringMode == EXHAUST_RINGS);
  CHECK(nh.config.failover.responseCodes[0] == 404);
  CHECK(nh.config.failover.responseCodes[1] == 503);
  CHECK(nh.config.failover.healthChecks[0] == PASSIVE);

  CHECK(nh.config.groups[0][0].host == "p1-cache.foo.com");
  CHECK(nh.config.groups[0][0].healthCheckUrl == "tcp://192.168.1.1:80");
  CHECK(nh.config.groups[0][0].weight == 1.0);
  CHECK(nh.config.groups[0][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][0].protocols[0].port == 80);
  CHECK(nh.config.groups[0][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][0].protocols[1].port == 443);

  CHECK(nh.config.groups[0][1].host == "p2-cache.foo.com");
  CHECK(nh.config.groups[0][1].healthCheckUrl == "tcp://192.168.1.2:80");
  CHECK(nh.config.groups[0][1].weight == 2.0);
  CHECK(nh.config.groups[0][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[0][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][1].protocols[1].port == 8443);

  CHECK(nh.config.groups[1][0].host == "s1-cache.bar.com");
  CHECK(nh.config.groups[1][0].healthCheckUrl == "tcp://192.168.2.1:80");
  CHECK(nh.config.groups[1][0].weight == 0.1);
  CHECK(nh.config.groups[1][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][0].protocols[0].port == 80);
  CHECK(nh.config.groups[1][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][0].protocols[1].port == 443);

  CHECK(nh.config.groups[1][1].host == "s2-cache.bar.com");
  CHECK(nh.config.groups[1][1].healthCheckUrl == "tcp://192.168.2.2:80");
  CHECK(nh.config.groups[1][1].weight == 0.9);
  CHECK(nh.config.groups[1][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[1][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][1].protocols[1].port == 8443);
}

TEST_CASE("5", "[loadConfig]")
{
  // default weight is 1.0 when no weight alias extension is added to the groups
  INFO("Test 2, loading a combined hosts and strategy file with no #include and no hosts alias extension for weight.")

  NextHopConfig nh;
  ts::Errata result;

  result = nh.loadConfig("unit-tests/combined_no_weight_alias_extension.yaml");
  INFO("messaage text: " + result.top().text());
  REQUIRE(result.top().getCode() == 0);

  CHECK(nh.config.policy == CONSISTENT_HASH);
  CHECK(nh.config.hashKey == PATH_QUERY);
  CHECK(nh.config.protocol == HTTP);
  CHECK(nh.config.failover.ringMode == EXHAUST_RINGS);
  CHECK(nh.config.failover.responseCodes[0] == 404);
  CHECK(nh.config.failover.responseCodes[1] == 503);
  CHECK(nh.config.failover.healthChecks[0] == PASSIVE);

  CHECK(nh.config.groups[0][0].host == "p1-cache.foo.com");
  CHECK(nh.config.groups[0][0].healthCheckUrl == "tcp://192.168.1.1:80");
  CHECK(nh.config.groups[0][0].weight == 1.0);
  CHECK(nh.config.groups[0][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][0].protocols[0].port == 80);
  CHECK(nh.config.groups[0][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][0].protocols[1].port == 443);

  CHECK(nh.config.groups[0][1].host == "p2-cache.foo.com");
  CHECK(nh.config.groups[0][1].healthCheckUrl == "tcp://192.168.1.2:80");
  CHECK(nh.config.groups[0][1].weight == 1.0);
  CHECK(nh.config.groups[0][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[0][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[0][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[0][1].protocols[1].port == 8443);

  CHECK(nh.config.groups[1][0].host == "s1-cache.bar.com");
  CHECK(nh.config.groups[1][0].healthCheckUrl == "tcp://192.168.2.1:80");
  CHECK(nh.config.groups[1][0].weight == 1.0);
  CHECK(nh.config.groups[1][0].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][0].protocols[0].port == 80);
  CHECK(nh.config.groups[1][0].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][0].protocols[1].port == 443);

  CHECK(nh.config.groups[1][1].host == "s2-cache.bar.com");
  CHECK(nh.config.groups[1][1].healthCheckUrl == "tcp://192.168.2.2:80");
  CHECK(nh.config.groups[1][1].weight == 1.0);
  CHECK(nh.config.groups[1][1].protocols[0].protocol == "http");
  CHECK(nh.config.groups[1][1].protocols[0].port == 8080);
  CHECK(nh.config.groups[1][1].protocols[1].protocol == "https");
  CHECK(nh.config.groups[1][1].protocols[1].port == 8443);
}
