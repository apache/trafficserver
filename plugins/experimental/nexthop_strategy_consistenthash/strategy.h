/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>
#include <mutex>
#include <algorithm>
#include <string>
#include <memory>
#include <unordered_map>
#include <stdint.h>
#include <time.h>
#include <yaml-cpp/yaml.h>
#include "tscore/ConsistentHash.h"
#include "ts/ts.h"
#include "ts/nexthop.h"
#include "ts/remap.h"
#include "healthstatus.h"

// TODO rename, move to respective sub-plugins
#define PLUGIN_NAME "nexthop_strategy_consistenthash.so"

constexpr const char *NH_DEBUG_TAG = "plugin_nexthop";

#define NH_Debug(tag, fmt, ...) TSDebug(tag, "[%s:%d]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define NH_Error(fmt, ...) TSError("(%s) [%s:%d]: " fmt, PLUGIN_NAME, __FILE__, __LINE__, ##__VA_ARGS__)
#define NH_Note(fmt, ...) TSDebug(NH_DEBUG_TAG, "[%s:%d]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

constexpr const char *policy_strings[] = {"NH_UNDEFINED", "NH_FIRST_LIVE", "NH_RR_STRICT",
                                          "NH_RR_IP",     "NH_RR_LATCHED", "NH_CONSISTENT_HASH"};

enum NHPolicyType {
  NH_UNDEFINED = 0,
  NH_FIRST_LIVE,      // first available nexthop
  NH_RR_STRICT,       // strict round robin
  NH_RR_IP,           // round robin by client ip.
  NH_RR_LATCHED,      // latched to available next hop.
  NH_CONSISTENT_HASH, // consistent hashing strategy.
  NH_PLUGIN,          // hashing strategy is a plugin
};

enum NHSchemeType { NH_SCHEME_NONE = 0, NH_SCHEME_HTTP, NH_SCHEME_HTTPS };

enum NHRingMode { NH_ALTERNATE_RING = 0, NH_EXHAUST_RING };

// response codes container
struct ResponseCodes {
  ResponseCodes(){};
  std::vector<short> codes;
  void
  add(short code)
  {
    codes.push_back(code);
  }
  bool
  contains(short code)
  {
    return std::binary_search(codes.begin(), codes.end(), code);
  }
  void
  sort()
  {
    std::sort(codes.begin(), codes.end());
  }
};

struct HealthChecks {
  bool active  = false;
  bool passive = false;
};

struct NHProtocol {
  NHSchemeType scheme;
  uint32_t port;
  std::string health_check_url;
};

struct HostRecord : ATSConsistentHashNode {
  std::mutex _mutex;
  std::string hostname;
  time_t failedAt;
  uint32_t failCount;
  time_t upAt;
  float weight;
  std::string hash_string;
  int host_index;
  int group_index;
  std::vector<std::shared_ptr<NHProtocol>> protocols;

  // construct without locking the _mutex.
  HostRecord()
  {
    hostname    = "";
    failedAt    = 0;
    failCount   = 0;
    upAt        = 0;
    weight      = 0;
    hash_string = "";
    host_index  = -1;
    group_index = -1;
    available   = true;
  }

  // copy constructor to avoid copying the _mutex.
  HostRecord(const HostRecord &o)
  {
    hostname    = o.hostname;
    failedAt    = o.failedAt;
    failCount   = o.failCount;
    upAt        = o.upAt;
    weight      = o.weight;
    hash_string = o.hash_string;
    host_index  = -1;
    group_index = -1;
    available   = true;
    protocols   = o.protocols;
  }

  // assign without copying the _mutex.
  HostRecord &
  operator=(const HostRecord &o)
  {
    hostname    = o.hostname;
    failedAt    = o.failedAt;
    upAt        = o.upAt;
    weight      = o.weight;
    hash_string = o.hash_string;
    host_index  = o.host_index;
    group_index = o.group_index;
    available   = o.available;
    protocols   = o.protocols;
    return *this;
  }

  // locks the record when marking this host down.
  void
  set_unavailable()
  {
    if (available) {
      std::lock_guard<std::mutex> lock(_mutex);
      failedAt  = time(nullptr);
      available = false;
    }
  }

  // locks the record when marking this host up.
  void
  set_available()
  {
    if (!available) {
      std::lock_guard<std::mutex> lock(_mutex);
      failedAt  = 0;
      failCount = 0;
      upAt      = time(nullptr);
      available = true;
    }
  }

  int
  getPort(NHSchemeType scheme) const
  {
    int port = 0;
    for (uint32_t i = 0; i < protocols.size(); i++) {
      if (protocols[i]->scheme == scheme) {
        port = protocols[i]->port;
        break;
      }
    }
    return port;
  }

  static std::string
  makeHostPort(const std::string& hostname, const int port)
  {
    return hostname + ":" + std::to_string(port);
  }

  std::string
  getHostPort(const int port) const
  {
    return makeHostPort(this->hostname, port);
  }
};

class NextHopSelectionStrategy : public TSNextHopSelectionStrategy
{
public:
  NextHopSelectionStrategy();
  NextHopSelectionStrategy(const std::string_view &name);
  virtual ~NextHopSelectionStrategy(){};
  bool Init(const YAML::Node &n);

  virtual void findNextHop(TSHttpTxn txnp, time_t now = 0) = 0;
  virtual void markNextHop(TSHttpTxn txnp, const char *hostname, const int port, const NHCmd status, const time_t now = 0);
  virtual bool nextHopExists(TSHttpTxn txnp);
  virtual bool responseIsRetryable(unsigned int current_retry_attempts, TSHttpStatus response_code);
  virtual bool onFailureMarkParentDown(TSHttpStatus response_code);
  virtual bool goDirect();
  virtual bool parentIsProxy();
protected:
  std::string strategy_name;
  bool go_direct           = true;
  bool parent_is_proxy     = true;
  bool ignore_self_detect  = false;
  NHSchemeType scheme      = NH_SCHEME_NONE;
  NHRingMode ring_mode     = NH_ALTERNATE_RING;
  ResponseCodes resp_codes;
  HealthChecks health_checks;
  NextHopHealthStatus passive_health;
  std::vector<std::vector<std::shared_ptr<HostRecord>>> host_groups;
  uint32_t max_simple_retries = 1;
  uint32_t groups             = 0;
  uint32_t grp_index          = 0;
  uint32_t hst_index          = 0;
  uint32_t num_parents        = 0;
  uint32_t distance           = 0; // index into the strategies list.
};
