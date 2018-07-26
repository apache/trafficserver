/** @file
 *
 *  Remap configuration file parsing.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <yaml-cpp/yaml.h>

enum SelectionStrategyHashKeyType {
  HASH_UNDEFINED,
  HASH_URL,
  HASH_URI,
  HASH_HOSTNAME,
  HASH_PATH,
  HASH_PATH_FRAGMENT,
  HASH_PATH_QUERY,
  HASH_CACHE_KEY
};

enum SelectionStrategyHealthCheckType { HEALTH_CHECK_UNDEFINED, HEALTH_CHECK_ACTIVE, HEALTH_CHECK_PASSIVE };

enum SelectionStrategyPolicy {
  POLICY_UNDEFINED,
  POLICY_FIRST_LIVE,
  POLICY_RR_STRICT,
  POLICY_RR_IP,
  POLICY_LATCHED,
  POLICY_CONSISTENT_HASH
};

enum SelectionStrategyProtocol { PROTO_UNDEFINED, PROTO_HTTP, PROTO_HTTPS };

enum SelectionStrategyRingMode {
  RING_MODE_UNDEFINED,
  RING_MODE_EXHAUST_RING,
  RING_MODE_ALTERNATE_RINGS

};

class RemapParentConfig
{
public:
  RemapParentConfig(){};
  ~RemapParentConfig(){};
  bool parse(const char *filename);
  bool loadConfig(const char *filename);
  SelectionStrategyHashKeyType
  getHashKeyType()
  {
    return hash_key_type;
  }
  SelectionStrategyPolicy
  getSelectionPolicy()
  {
    return selection_policy_type;
  }

private:
  SelectionStrategyHashKeyType setHashKeyType(std::string &value);
  SelectionStrategyPolicy setPolicy(std::string &value);

  YAML::Node config;
  std::map<std::string, SelectionStrategyHashKeyType> hash_key_types = {
    {"undefined", SelectionStrategyHashKeyType::HASH_UNDEFINED},
    {"url", SelectionStrategyHashKeyType::HASH_URL},
    {"uri", SelectionStrategyHashKeyType::HASH_URI},
    {"hostname", SelectionStrategyHashKeyType::HASH_HOSTNAME},
    {"path", SelectionStrategyHashKeyType::HASH_PATH},
    {"path+fragment", SelectionStrategyHashKeyType::HASH_PATH_FRAGMENT},
    {"path+query", SelectionStrategyHashKeyType::HASH_PATH_QUERY},
    {"cache_key", SelectionStrategyHashKeyType::HASH_CACHE_KEY}};

  std::map<std::string, SelectionStrategyHealthCheckType> health_check_types{
    {"undefined", SelectionStrategyHealthCheckType::HEALTH_CHECK_UNDEFINED},
    {"active", SelectionStrategyHealthCheckType::HEALTH_CHECK_ACTIVE},
    {"passive", SelectionStrategyHealthCheckType::HEALTH_CHECK_PASSIVE}};

  std::map<std::string, SelectionStrategyProtocol> protocol_types{{"undefined", SelectionStrategyProtocol::PROTO_UNDEFINED},
                                                                  {"http", SelectionStrategyProtocol::PROTO_HTTP},
                                                                  {"https", SelectionStrategyProtocol::PROTO_HTTPS}};

  std::map<std::string, SelectionStrategyRingMode> ring_mode_types{
    {"undefined", SelectionStrategyRingMode::RING_MODE_UNDEFINED},
    {"exhaust_ring", SelectionStrategyRingMode::RING_MODE_EXHAUST_RING},
    {"alternate_rings", SelectionStrategyRingMode::RING_MODE_ALTERNATE_RINGS}};

  std::map<std::string, SelectionStrategyPolicy> selection_policy_types = {
    {"undefined", SelectionStrategyPolicy::POLICY_UNDEFINED}, {"first_live", SelectionStrategyPolicy::POLICY_FIRST_LIVE},
    {"rr_strict", SelectionStrategyPolicy::POLICY_RR_STRICT}, {"rr_ip", SelectionStrategyPolicy::POLICY_RR_IP},
    {"latched", SelectionStrategyPolicy::POLICY_LATCHED},     {"consistent_hash", SelectionStrategyPolicy::POLICY_CONSISTENT_HASH}};

  SelectionStrategyHashKeyType hash_key_type;
  SelectionStrategyHealthCheckType health_check_type;
  SelectionStrategyProtocol protocol_type;
  SelectionStrategyRingMode ring_mode_type;
  SelectionStrategyPolicy selection_policy_type;
};
