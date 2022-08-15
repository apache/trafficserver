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
#include "ts/parentselectdefs.h"
#include "ts/remap.h"
#include "healthstatus.h"

// TODO rename, move to respective sub-plugins
#define PLUGIN_NAME "pparent_select"

constexpr const char *PL_NH_DEBUG_TAG = PLUGIN_NAME;

// ring mode strings
extern const std::string_view alternate_rings;
extern const std::string_view exhaust_rings;
extern const std::string_view peering_rings;

// health check strings
extern const std::string_view active_health_check;
extern const std::string_view passive_health_check;

#define PL_NH_Debug(tag, fmt, ...) TSDebug(tag, "[%s:%d]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define PL_NH_Error(fmt, ...) TSError("(%s) [%s:%d]: " fmt, PLUGIN_NAME, __FILE__, __LINE__, ##__VA_ARGS__)
#define PL_NH_Note(fmt, ...) TSDebug(PL_NH_DEBUG_TAG, "[%s:%d]: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

constexpr const char *policy_strings[] = {"PL_NH_UNDEFINED", "PL_NH_FIRST_LIVE", "PL_NH_RR_STRICT",
                                          "PL_NH_RR_IP",     "PL_NH_RR_LATCHED", "PL_NH_CONSISTENT_HASH"};

constexpr const TSHttpStatus STATUS_CONNECTION_FAILURE = static_cast<TSHttpStatus>(0);

enum PLNHPolicyType {
  PL_NH_UNDEFINED = 0,
  PL_NH_FIRST_LIVE,      // first available nexthop
  PL_NH_RR_STRICT,       // strict round robin
  PL_NH_RR_IP,           // round robin by client ip.
  PL_NH_RR_LATCHED,      // latched to available next hop.
  PL_NH_CONSISTENT_HASH, // consistent hashing strategy.
  PL_NH_PLUGIN,          // hashing strategy is a plugin
};

enum PLNHSchemeType { PL_NH_SCHEME_NONE = 0, PL_NH_SCHEME_HTTP, PL_NH_SCHEME_HTTPS };

enum PLNHRingMode { PL_NH_ALTERNATE_RING = 0, PL_NH_EXHAUST_RING, PL_NH_PEERING_RING };

// response codes container
struct PLResponseCodes {
  PLResponseCodes(){};
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

struct PLHealthChecks {
  bool active  = false;
  bool passive = false;
};

struct PLNHProtocol {
  PLNHSchemeType scheme = PL_NH_SCHEME_NONE;
  uint32_t port         = 0;
  std::string health_check_url;
};

struct PLHostRecord : ATSConsistentHashNode {
  std::mutex _mutex;
  std::string hostname;
  std::atomic<time_t> failedAt;
  std::atomic<uint32_t> failCount;
  std::atomic<time_t> upAt;
  float weight;
  std::string hash_string;
  int host_index;
  int group_index;
  bool self = false;
  std::vector<std::shared_ptr<PLNHProtocol>> protocols;

  // construct without locking the _mutex.
  PLHostRecord()
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
  PLHostRecord(const PLHostRecord &o)
  {
    hostname    = o.hostname;
    failedAt    = o.failedAt.load();
    failCount   = o.failCount.load();
    upAt        = o.upAt.load();
    weight      = o.weight;
    hash_string = o.hash_string;
    host_index  = o.host_index;
    group_index = o.group_index;
    available   = o.available.load();
    protocols   = o.protocols;
    self        = o.self;
  }

  // assign without copying the _mutex.
  PLHostRecord &
  operator=(const PLHostRecord &o)
  {
    hostname    = o.hostname;
    failedAt    = o.failedAt.load();
    failCount   = o.failCount.load();
    upAt        = o.upAt.load();
    weight      = o.weight;
    hash_string = o.hash_string;
    host_index  = o.host_index;
    group_index = o.group_index;
    available   = o.available.load();
    protocols   = o.protocols;
    self        = o.self;
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
  getPort(PLNHSchemeType scheme) const
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
  makeHostPort(const std::string_view hostname, const in_port_t port)
  {
    return std::string(hostname) + ":" + std::to_string(port);
  }

  std::string
  getHostPort(const in_port_t port) const
  {
    return makeHostPort(this->hostname, port);
  }
};

class TSNextHopSelectionStrategy
{
public:
  TSNextHopSelectionStrategy(){};
  virtual ~TSNextHopSelectionStrategy(){};

  virtual const char *name()                                                                        = 0;
  virtual void next(TSHttpTxn txnp, void *strategyTxn, const char **out_hostname, size_t *out_hostname_len, in_port_t *out_port,
                    bool *out_retry, bool *out_no_cache, time_t now = 0)                            = 0;
  virtual void mark(TSHttpTxn txnp, void *strategyTxn, const char *hostname, const size_t hostname_len, const in_port_t port,
                    const PLNHCmd status, const time_t now = 0)                                     = 0;
  virtual bool nextHopExists(TSHttpTxn txnp)                                                        = 0;
  virtual bool codeIsFailure(TSHttpStatus response_code)                                            = 0;
  virtual bool responseIsRetryable(unsigned int current_retry_attempts, TSHttpStatus response_code) = 0;
  virtual bool onFailureMarkParentDown(TSHttpStatus response_code)                                  = 0;

  virtual bool goDirect()      = 0;
  virtual bool parentIsProxy() = 0;

  virtual void *newTxn()              = 0;
  virtual void deleteTxn(void *state) = 0;
};

class PLNextHopSelectionStrategy : public TSNextHopSelectionStrategy
{
public:
  PLNextHopSelectionStrategy() = delete;
  PLNextHopSelectionStrategy(const std::string_view &name, const YAML::Node &n);
  virtual ~PLNextHopSelectionStrategy(){};

  void next(TSHttpTxn txnp, void *strategyTxn, const char **out_hostname, size_t *out_hostname_len, in_port_t *out_port,
            bool *out_retry, bool *out_no_cache, time_t now = 0) override = 0;
  void mark(TSHttpTxn txnp, void *strategyTxn, const char *hostname, const size_t hostname_len, const in_port_t port,
            const PLNHCmd status, const time_t now = 0) override          = 0;
  bool nextHopExists(TSHttpTxn txnp) override;
  bool codeIsFailure(TSHttpStatus response_code) override;
  bool responseIsRetryable(unsigned int current_retry_attempts, TSHttpStatus response_code) override;
  bool onFailureMarkParentDown(TSHttpStatus response_code) override;
  bool goDirect() override;
  bool parentIsProxy() override;
  const char *
  name() override
  {
    return strategy_name.c_str();
  };
  void *newTxn() override              = 0;
  void deleteTxn(void *state) override = 0;

protected:
  std::string strategy_name;
  bool go_direct          = true;
  bool parent_is_proxy    = true;
  bool ignore_self_detect = false;
  bool cache_peer_result  = true;
  PLNHSchemeType scheme   = PL_NH_SCHEME_NONE;
  PLNHRingMode ring_mode  = PL_NH_ALTERNATE_RING;
  PLResponseCodes resp_codes;     // simple retry codes
  PLResponseCodes markdown_codes; // unavailable server retry and markdown codes

  PLHealthChecks health_checks;
  PLNextHopHealthStatus passive_health;
  std::vector<std::vector<std::shared_ptr<PLHostRecord>>> host_groups;
  uint32_t max_simple_retries      = 1;
  uint32_t max_unavailable_retries = 1;
  uint32_t groups                  = 0;
  uint32_t grp_index               = 0;
  uint32_t hst_index               = 0;
  uint32_t num_parents             = 0;
  uint32_t distance                = 0; // index into the strategies list.
};
