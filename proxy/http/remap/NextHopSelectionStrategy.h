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

#include "ParentSelection.h"

#ifndef _NH_UNIT_TESTS_
#define NH_Debug(tag, ...) Debug(tag, __VA_ARGS__)
#define NH_Error(...) DiagsError(DL_Error, __VA_ARGS__)
#define NH_Note(...) DiagsError(DL_Note, __VA_ARGS__)
#define NH_Warn(...) DiagsError(DL_Warning, __VA_ARGS__)
#else
#include "unit-tests/nexthop_test_stubs.h"
#endif /* _NH_UNIT_TESTS_ */

constexpr const char *NH_DEBUG_TAG = "next_hop";

namespace YAML
{
class Node;
}

enum NHPolicyType {
  NH_UNDEFINED = 0,
  NH_FIRST_LIVE,     // first available nexthop
  NH_RR_STRICT,      // strict round robin
  NH_RR_IP,          // round robin by client ip.
  NH_RR_LATCHED,     // latched to available next hop.
  NH_CONSISTENT_HASH // consistent hashing strategy.
};

enum NHSchemeType { NH_SCHEME_NONE = 0, NH_SCHEME_HTTP, NH_SCHEME_HTTPS };

enum NHRingMode { NH_ALTERNATE_RING = 0, NH_EXHAUST_RING };

enum NH_HHealthCheck { NH_ACTIVE, NH_PASSIVE };

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
    hash_string = "";
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
  getPort(NHSchemeType scheme)
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
};

class NextHopSelectionStrategy
{
public:
  NextHopSelectionStrategy();
  NextHopSelectionStrategy(const std::string_view &name, const NHPolicyType &type);
  virtual ~NextHopSelectionStrategy(){};
  bool Init(const YAML::Node &n);
  virtual void findNextHop(const uint64_t sm_id, ParentResult &result, RequestData &rdata, const uint64_t fail_threshold,
                           const uint64_t retry_time, time_t now = 0) = 0;
  void markNextHopDown(const uint64_t sm_id, ParentResult &result, const uint64_t fail_threshold, const uint64_t retry_time,
                       time_t now = 0);
  void markNextHopUp(const uint64_t sm_id, ParentResult &result);
  bool nextHopExists(const uint64_t sm_id);

  std::string strategy_name;
  bool go_direct           = true;
  bool parent_is_proxy     = true;
  bool ignore_self_detect  = false;
  NHPolicyType policy_type = NH_UNDEFINED;
  NHSchemeType scheme      = NH_SCHEME_NONE;
  NHRingMode ring_mode     = NH_ALTERNATE_RING;
  ResponseCodes resp_codes;
  HealthChecks health_checks;
  std::vector<std::vector<std::shared_ptr<HostRecord>>> host_groups;
  uint32_t max_simple_retries = 1;
  uint32_t groups             = 0;
  uint32_t grp_index          = 0;
  uint32_t hst_index          = 0;
  uint32_t num_parents        = 0;
  uint32_t distance           = 0; // index into the strategies list.
};
