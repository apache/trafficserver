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

#pragma once

#include <yaml-cpp/yaml.h>
#include <unordered_set>
#include <vector>
#include <iostream>

#include "ts/EnumDescriptor.h"
#include "tsconfig/Errata.h"

enum NextHopSelectionPolicy { POLICY_UNDEFINED = 0, CONSISTENT_HASH, FIRST_LIVE, RR_STRICT, RR_IP, LATCHED };
static TsEnumDescriptor POLICY_DESCRIPTOR = {
  {{"consistent_hash", 1}, {"first_live", 2}, {"rr_strict", 3}, {"rr_ip", 4}, {"latched", 5}}};

enum NextHopHashKey {
  KEY_UNDEFINED = 0,
  CACHE_KEY,
  URI,
  URL,
  HOSTNAME,
  PATH,
  PATH_QUERY,
  PATH_FRAGMENT,
};
static TsEnumDescriptor HASH_KEY_DESCRIPTOR = {
  {{"cache_key", 1}, {"uri", 2}, {"url", 3}, {"hostname", 4}, {"path", 5}, {"path+query", 6}, {"path+fragment", 7}}};

enum NextHopProtocol { PROTOCOL_UNDEFINED = 0, HTTP, HTTPS };
static TsEnumDescriptor PROTOCOL_DESCRIPTOR = {{{"http", 1}, {"https", 2}}};

enum NextHopRingMode { RING_MODE_UNDEFINED = 0, ALTERNATE_RINGS, EXHAUST_RINGS };
static TsEnumDescriptor RING_MODE_DESCRIPTOR = {{{"alternate_rings", 1}, {"exhaust_rings", 2}}};

enum NextHopHealthCheck { HEALTH_UNDEFINED = 0, ACTIVE, PASSIVE };
static TsEnumDescriptor HEALTH_CHECK_DESCRIPTOR = {{{"active", 1}, {"passive", 2}}};

// strategy keys
constexpr char NH_alias_extension[] = "<<";
constexpr char NH_strategy[]        = "strategy";
constexpr char NH_policy[]          = "policy";
constexpr char NH_hashKey[]         = "hash_key";
constexpr char NH_groups[]          = "groups";
constexpr char NH_protocol[]        = "protocol";
constexpr char NH_failover[]        = "failover";
// failover keys
constexpr char NH_ringMode[]      = "ring_mode";
constexpr char NH_responseCodes[] = "response_codes";
constexpr char NH_health_Check[]  = "health_check";
// host keys
constexpr char NH_host[]        = "host";
constexpr char NH_healthCheck[] = "healthcheck";
constexpr char NH_url[]         = "url";
constexpr char NH_weight[]      = "weight";
constexpr char NH_http[]        = "http";
constexpr char NH_https[]       = "https";

// valid strategy keys
static std::set<std::string> valid_strategy_keys = {NH_policy, NH_hashKey, NH_groups, NH_protocol, NH_failover};

// valid failover keys
static std::set<std::string> valid_failover_keys = {NH_ringMode, NH_responseCodes, NH_health_Check};

// valid host keys
static std::set<std::string> valid_host_keys = {NH_alias_extension, NH_host, NH_protocol, NH_healthCheck, NH_weight};

struct NextHopHostProtocols {
  NextHopHostProtocols() : port(0) {}
  std::string protocol;
  unsigned int port;
};

struct NextHopHost {
  NextHopHost() : weight(1.0) {}
  std::string host;
  std::string healthCheckUrl;
  std::vector<NextHopHostProtocols> protocols;
  double weight;
};

struct NextHopFailOver {
  NextHopRingMode ringMode;
  std::vector<int> responseCodes;
  std::vector<NextHopHealthCheck> healthChecks;
};

struct NextHopStrategyConfig {
  NextHopStrategyConfig() : policy{POLICY_UNDEFINED}, hashKey{PATH}, protocol{HTTP} {}
  NextHopSelectionPolicy policy;
  NextHopHashKey hashKey;
  NextHopProtocol protocol;
  NextHopFailOver failover;

  std::vector<std::vector<NextHopHost>> groups;
  ts::Errata errata;
};

class NextHopConfig
{
  void loadFile(const std::string fileName, std::stringstream &buf, std::unordered_set<std::string> &include_once);
  ts::Errata errata;

public:
  NextHopConfig() {}

  NextHopStrategyConfig config;
  ts::Errata loadConfig(const char *fileName);
};
