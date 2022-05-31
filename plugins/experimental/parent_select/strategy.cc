/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "strategy.h"
#include "consistenthash.h"
#include "util.h"

#include <cinttypes>
#include <string>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <cstring>

#include <sys/stat.h>
#include <dirent.h>

#include <yaml-cpp/yaml.h>

#include "tscore/HashSip.h"
#include "tscore/ConsistentHash.h"
#include "tscore/ink_assert.h"
#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/parentselectdefs.h"

//
// NextHopSelectionStrategy.cc
//

// ring mode strings
const std::string_view alternate_rings = "alternate_ring";
const std::string_view exhaust_rings   = "exhaust_ring";
const std::string_view peering_rings   = "peering_ring";

// health check strings
const std::string_view active_health_check  = "active";
const std::string_view passive_health_check = "passive";

PLNextHopSelectionStrategy::PLNextHopSelectionStrategy(const std::string_view &name, const YAML::Node &n) : strategy_name(name)
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "PLNextHopSelectionStrategy constructor calling");
  std::string self_host;
  bool self_host_used = false;

  try {
    // scheme is optional, and strategies with no scheme will match hosts with no scheme
    if (n["scheme"]) {
      auto scheme_val = n["scheme"].Scalar();
      if (scheme_val == "http") {
        scheme = PL_NH_SCHEME_HTTP;
      } else if (scheme_val == "https") {
        scheme = PL_NH_SCHEME_HTTPS;
      } else {
        PL_NH_Note("Invalid scheme '%s' for strategy '%s', setting to NONE", scheme_val.c_str(), strategy_name.c_str());
      }
    }

    // go_direct config.
    if (n["go_direct"]) {
      go_direct = n["go_direct"].as<bool>();
    }

    // parent_is_proxy config.
    if (n["parent_is_proxy"]) {
      parent_is_proxy = n["parent_is_proxy"].as<bool>();
    }

    // ignore_self_detect
    if (n["ignore_self_detect"]) {
      ignore_self_detect = n["ignore_self_detect"].as<bool>();
    }

    if (n["cache_peer_result"]) {
      cache_peer_result = n["cache_peer_result"].as<bool>();
    }

    // failover node.
    YAML::Node failover_node;
    if (n["failover"]) {
      failover_node = n["failover"];
      if (failover_node["ring_mode"]) {
        auto ring_mode_val = failover_node["ring_mode"].Scalar();
        if (ring_mode_val == alternate_rings) {
          ring_mode = PL_NH_ALTERNATE_RING;
        } else if (ring_mode_val == exhaust_rings) {
          ring_mode = PL_NH_EXHAUST_RING;
        } else if (ring_mode_val == peering_rings) {
          ring_mode            = PL_NH_PEERING_RING;
          YAML::Node self_node = failover_node["self"];
          if (self_node) {
            self_host = self_node.Scalar();
            PL_NH_Debug(PL_NH_DEBUG_TAG, "%s is self", self_host.c_str());
          }
        } else {
          ring_mode = PL_NH_ALTERNATE_RING;
          PL_NH_Note("Invalid 'ring_mode' value, '%s', for the strategy named '%s', using default '%s'.", ring_mode_val.c_str(),
                     strategy_name.c_str(), alternate_rings.data());
        }
      }
      if (failover_node["max_simple_retries"]) {
        max_simple_retries = failover_node["max_simple_retries"].as<int>();
      }

      if (failover_node["max_unavailable_retries"]) {
        max_unavailable_retries = failover_node["max_unavailable_retries"].as<int>();
      }

      YAML::Node resp_codes_node;
      // connection failures are always failure and retryable (pending retries)
      resp_codes.add(STATUS_CONNECTION_FAILURE);
      if (failover_node["response_codes"]) {
        resp_codes_node = failover_node["response_codes"];
        if (resp_codes_node.Type() != YAML::NodeType::Sequence) {
          PL_NH_Error("Error in the response_codes definition for the strategy named '%s', skipping response_codes.",
                      strategy_name.c_str());
        } else {
          for (auto &&k : resp_codes_node) {
            auto code = k.as<int>();
            if (code > 300 && code < 599) {
              resp_codes.add(code);
            } else {
              PL_NH_Note("Skipping invalid response code '%d' for the strategy named '%s'.", code, strategy_name.c_str());
            }
          }
          resp_codes.sort();
        }
      }
      YAML::Node markdown_codes_node;
      if (failover_node["markdown_codes"]) {
        markdown_codes_node = failover_node["markdown_codes"];
        if (markdown_codes_node.Type() != YAML::NodeType::Sequence) {
          PL_NH_Error("Error in the markdown_codes definition for the strategy named '%s', skipping markdown_codes.",
                      strategy_name.c_str());
        } else {
          for (auto &&k : markdown_codes_node) {
            auto code = k.as<int>();
            if (code > 300 && code < 599) {
              markdown_codes.add(code);
            } else {
              PL_NH_Note("Skipping invalid markdown response code '%d' for the strategy named '%s'.", code, strategy_name.c_str());
            }
          }
          markdown_codes.sort();
        }
      }
      YAML::Node health_check_node;
      if (failover_node["health_check"]) {
        health_check_node = failover_node["health_check"];
        if (health_check_node.Type() != YAML::NodeType::Sequence) {
          PL_NH_Error("Error in the health_check definition for the strategy named '%s', skipping health_checks.",
                      strategy_name.c_str());
        } else {
          for (auto it = health_check_node.begin(); it != health_check_node.end(); ++it) {
            auto health_check = it->as<std::string>();
            if (health_check.compare(active_health_check) == 0) {
              health_checks.active = true;
            }
            if (health_check.compare(passive_health_check) == 0) {
              health_checks.passive = true;
            }
          }
        }
      }
    }

    // parse and load the host data
    YAML::Node groups_node;
    if (n["groups"]) {
      groups_node = n["groups"];
      // a groups list is required.
      if (groups_node.Type() != YAML::NodeType::Sequence) {
        throw std::invalid_argument("Invalid groups definition, expected a sequence, '" + strategy_name + "' cannot be loaded.");
      } else {
        uint32_t grp_size = groups_node.size();
        if (grp_size > PL_NH_MAX_GROUP_RINGS) {
          PL_NH_Note(
            "the groups list exceeds the maximum of %d for the strategy '%s'. Only the first %d groups will be configured.",
            PL_NH_MAX_GROUP_RINGS, strategy_name.c_str(), PL_NH_MAX_GROUP_RINGS);
          groups = PL_NH_MAX_GROUP_RINGS;
        } else {
          groups = groups_node.size();
        }
        // resize the hosts vector.
        host_groups.reserve(groups);
        // loop through the groups
        for (unsigned int grp = 0; grp < groups; ++grp) {
          YAML::Node hosts_list = groups_node[grp];

          // a list of hosts is required.
          if (hosts_list.Type() != YAML::NodeType::Sequence) {
            throw std::invalid_argument("Invalid hosts definition, expected a sequence, '" + strategy_name + "' cannot be loaded.");
          } else {
            // loop through the hosts list.
            std::vector<std::shared_ptr<PLHostRecord>> hosts_inner;

            for (unsigned int hst = 0; hst < hosts_list.size(); ++hst) {
              std::shared_ptr<PLHostRecord> host_rec = std::make_shared<PLHostRecord>(hosts_list[hst].as<PLHostRecord>());
              host_rec->group_index                  = grp;
              host_rec->host_index                   = hst;
              if (self_host == host_rec->hostname ||
                  TSHostnameIsSelf(host_rec->hostname.c_str(), host_rec->hostname.size()) == TS_SUCCESS) {
                if (ring_mode == PL_NH_PEERING_RING && grp != 0) {
                  throw std::invalid_argument("self host (" + self_host +
                                              ") can only appear in first host group for peering ring mode");
                }
                TSHostStatusSet(host_rec->hostname.c_str(), host_rec->hostname.size(), TSHostStatus::TS_HOST_STATUS_DOWN, 0,
                                static_cast<unsigned int>(TS_HOST_STATUS_SELF_DETECT));
                host_rec->self = true;
                self_host_used = true;
              }
              hosts_inner.push_back(std::move(host_rec));
              num_parents++;
            }
            passive_health.insert(hosts_inner);
            host_groups.push_back(std::move(hosts_inner));
          }
        }
      }
    }
    if (!self_host.empty() && !self_host_used) {
      throw std::invalid_argument("self host (" + self_host + ") does not appear in the first (peer) group");
    }
  } catch (std::exception &ex) {
    throw std::invalid_argument("Error parsing strategy named '" + strategy_name + "' due to '" + ex.what() +
                                "', this strategy will be ignored.");
  }
}

bool
PLNextHopSelectionStrategy::nextHopExists(TSHttpTxn txnp)
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "nhplugin nextHopExists calling");

  const int64_t sm_id = TSHttpTxnIdGet(txnp);

  for (uint32_t gg = 0; gg < groups; gg++) {
    for (auto &hh : host_groups[gg]) {
      PLHostRecord *p = hh.get();
      if (p->available) {
        PL_NH_Debug(PL_NH_DEBUG_TAG,
                    "[%" PRIu64 "] found available next hop %.*s (this is NOT necessarily the parent which will be selected, just "
                    "the first available parent found)",
                    sm_id, int(p->hostname.size()), p->hostname.c_str());
        return true;
      }
    }
  }
  return false;
}

bool
PLNextHopSelectionStrategy::codeIsFailure(TSHttpStatus response_code)
{
  return this->resp_codes.contains(response_code) || this->markdown_codes.contains(response_code);
}

bool
PLNextHopSelectionStrategy::responseIsRetryable(unsigned int current_retry_attempts, TSHttpStatus response_code)
{
  return (current_retry_attempts < this->num_parents) &&
         ((this->resp_codes.contains(response_code) && current_retry_attempts < this->max_simple_retries) ||
          (this->markdown_codes.contains(response_code) && current_retry_attempts < this->max_unavailable_retries));
}

bool
PLNextHopSelectionStrategy::onFailureMarkParentDown(TSHttpStatus response_code)
{
  return this->markdown_codes.contains(response_code);
}

bool
PLNextHopSelectionStrategy::goDirect()
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "nhplugin goDirect calling");
  return this->go_direct;
}

bool
PLNextHopSelectionStrategy::parentIsProxy()
{
  PL_NH_Debug(PL_NH_DEBUG_TAG, "nhplugin parentIsProxy calling");
  return this->parent_is_proxy;
}

namespace YAML
{
template <> struct convert<PLHostRecord> {
  static bool
  decode(const Node &node, PLHostRecord &nh)
  {
    YAML::Node nd;
    bool merge_tag_used = false;

    // check for YAML merge tag.
    if (node["<<"]) {
      nd             = node["<<"];
      merge_tag_used = true;
    } else {
      nd = node;
    }

    // lookup the hostname
    if (nd["host"]) {
      nh.hostname = nd["host"].Scalar();
    } else {
      throw std::invalid_argument("Invalid host definition, missing host name.");
    }

    // lookup the port numbers supported by this host.
    YAML::Node proto = nd["protocol"];

    if (proto.Type() != YAML::NodeType::Sequence) {
      throw std::invalid_argument("Invalid host protocol definition, expected a sequence.");
    } else {
      for (auto &&ii : proto) {
        const YAML::Node &protocol_node  = ii;
        std::shared_ptr<PLNHProtocol> pr = std::make_shared<PLNHProtocol>(protocol_node.as<PLNHProtocol>());
        nh.protocols.push_back(std::move(pr));
      }
    }

    // get the host's weight
    YAML::Node weight;
    if (merge_tag_used) {
      weight    = node["weight"];
      nh.weight = weight.as<float>();
    } else if ((weight = nd["weight"])) {
      nh.weight = weight.as<float>();
    } else {
      PL_NH_Note("No weight is defined for the host '%s', using default 1.0", nh.hostname.data());
      nh.weight = 1.0;
    }

    // get the host's optional hash_string
    YAML::Node hash;
    if ((hash = nd["hash_string"])) {
      nh.hash_string = hash.Scalar();
    }

    return true;
  }
};

template <> struct convert<PLNHProtocol> {
  static bool
  decode(const Node &node, PLNHProtocol &nh)
  {
    // scheme is optional, and strategies with no scheme will match hosts with no scheme
    if (node["scheme"]) {
      const auto scheme_val = node["scheme"].Scalar();
      if (scheme_val == "http") {
        nh.scheme = PL_NH_SCHEME_HTTP;
      } else if (scheme_val == "https") {
        nh.scheme = PL_NH_SCHEME_HTTPS;
      } else {
        PL_NH_Note("Invalid scheme '%s' for protocol, setting to NONE", scheme_val.c_str());
      }
    }
    if (node["port"]) {
      nh.port = node["port"].as<int>();
      if (nh.port <= 0 || nh.port > 65535) {
        throw YAML::ParserException(node["port"].Mark(), "port number must be in (inclusive) range 1 - 65,536");
      }
    } else {
      throw YAML::ParserException(node["port"].Mark(),
                                  "no port is defined, a port number must be defined within (inclusive) range 1 - 65,536");
    }
    if (node["health_check_url"]) {
      nh.health_check_url = node["health_check_url"].Scalar();
    }
    return true;
  }
};
}; // namespace YAML
