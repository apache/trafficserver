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
#include <mutex>

#include <utility>

#include "ts/apidefs.h"
#include "proxy/ParentSelection.h"
#include "proxy/http/HttpTransact.h"

#ifndef _NH_UNIT_TESTS_
#define NH_Dbg(ctl, ...) Dbg(ctl, __VA_ARGS__)
#define NH_Error(...)    DiagsError(DL_Error, __VA_ARGS__)
#define NH_Note(...)     DiagsError(DL_Note, __VA_ARGS__)
#define NH_Warn(...)     DiagsError(DL_Warning, __VA_ARGS__)
#else
#include "../../../../src/proxy/http/remap/unit-tests/nexthop_test_stubs.h"
#endif /* _NH_UNIT_TESTS_ */

extern DbgCtl NH_DBG_CTL;

namespace ts
{
namespace Yaml
{
  class Map;
}
} // namespace ts

enum class NHCmd { MARK_UP, MARK_DOWN };

struct NHHealthStatus {
  virtual bool isNextHopAvailable(TSHttpTxn txn, const char *hostname, const int port, void *ih = nullptr) = 0;
  virtual void markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, void *ih = nullptr,
                           const time_t now = 0)                                                           = 0;
  virtual ~NHHealthStatus() {}
};

enum class NHPolicyType {
  UNDEFINED = 0,
  FIRST_LIVE,     // first available nexthop
  RR_STRICT,      // strict round robin
  RR_IP,          // round robin by client ip.
  RR_LATCHED,     // latched to available next hop.
  CONSISTENT_HASH // consistent hashing strategy.
};

enum class NHSchemeType { NONE = 0, HTTP, HTTPS };

enum class NHRingMode { ALTERNATE_RING = 0, EXHAUST_RING, PEERING_RING };

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
  NHSchemeType scheme = NHSchemeType::NONE;
  uint32_t     port   = 0;
  std::string  health_check_url;
};

struct HostRecordCfg {
  std::string                              hostname;
  std::vector<std::shared_ptr<NHProtocol>> protocols;
  float                                    weight{0};
  std::string                              hash_string;
};

struct HostRecord : public ATSConsistentHashNode, public HostRecordCfg {
  std::mutex            _mutex;
  std::atomic<time_t>   failedAt{0};
  std::atomic<uint32_t> failCount{0};
  std::atomic<time_t>   upAt{0};
  int                   host_index{-1};
  int                   group_index{-1};
  bool                  self{false};

  explicit HostRecord(HostRecordCfg &&o) : HostRecordCfg(std::move(o)) {}

  // No copying or moving.
  HostRecord(const HostRecord &)            = delete;
  HostRecord &operator=(const HostRecord &) = delete;

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
  makeHostPort(const std::string &hostname, const int port)
  {
    return hostname + ":" + std::to_string(port);
  }

  std::string
  getHostPort(const int port) const
  {
    return makeHostPort(this->hostname, port);
  }
};

class NextHopHealthStatus : public NHHealthStatus
{
public:
  void insert(std::vector<std::shared_ptr<HostRecord>> &hosts);
  bool isNextHopAvailable(TSHttpTxn txn, const char *hostname, const int port, void *ih = nullptr) override;
  void markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, void *ih = nullptr,
                   const time_t now = 0) override;
  NextHopHealthStatus(){};

private:
  std::unordered_map<std::string, std::shared_ptr<HostRecord>> host_map;
};

class NextHopSelectionStrategy
{
public:
  NextHopSelectionStrategy() = delete;
  NextHopSelectionStrategy(const std::string_view &name, const NHPolicyType &type, ts::Yaml::Map &n);
  virtual ~NextHopSelectionStrategy(){};
  virtual void findNextHop(TSHttpTxn txnp, void *ih = nullptr, time_t now = 0) = 0;
  void         markNextHop(TSHttpTxn txnp, const char *hostname, const int port, const NHCmd status, void *ih = nullptr,
                           const time_t now = 0);
  bool         nextHopExists(TSHttpTxn txnp, void *ih = nullptr);

  void setHostHeader(TSHttpTxn txnp, const char *hostname);

  virtual ParentRetry_t responseIsRetryable(int64_t sm_id, HttpTransact::CurrentInfo &current_info, HTTPStatus response_code);

  void retryComplete(TSHttpTxn txn, const char *hostname, const int port);

  std::string                                           strategy_name;
  bool                                                  go_direct          = true;
  bool                                                  parent_is_proxy    = true;
  bool                                                  ignore_self_detect = false;
  bool                                                  cache_peer_result  = true;
  bool                                                  host_override      = false;
  bool                                                  use_pristine       = false;
  NHPolicyType                                          policy_type        = NHPolicyType::UNDEFINED;
  NHSchemeType                                          scheme             = NHSchemeType::NONE;
  NHRingMode                                            ring_mode          = NHRingMode::ALTERNATE_RING;
  ResponseCodes                                         resp_codes;     // simple retry codes
  ResponseCodes                                         markdown_codes; // unavailable server retry and markdown codes
  HealthChecks                                          health_checks;
  NextHopHealthStatus                                   passive_health;
  std::vector<std::vector<std::shared_ptr<HostRecord>>> host_groups;
  uint32_t                                              max_simple_retries      = 1;
  uint32_t                                              max_unavailable_retries = 1;
  uint32_t                                              groups                  = 0;
  uint32_t                                              grp_index               = 0;
  uint32_t                                              hst_index               = 0;
  uint32_t                                              num_parents             = 0;
  uint32_t                                              distance                = 0; // index into the strategies list.
};
