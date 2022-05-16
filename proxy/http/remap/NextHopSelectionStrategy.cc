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

#include <optional>

#include <yaml-cpp/yaml.h>
#include <YamlCfg.h>
#include "I_Machine.h"
#include "HttpSM.h"
#include "NextHopSelectionStrategy.h"

// ring mode strings
constexpr std::string_view alternate_rings = "alternate_ring";
constexpr std::string_view exhaust_rings   = "exhaust_ring";
constexpr std::string_view peering_rings   = "peering_ring";

// health check strings
constexpr std::string_view active_health_check  = "active";
constexpr std::string_view passive_health_check = "passive";

constexpr const char *policy_strings[] = {"NH_UNDEFINED", "NH_FIRST_LIVE", "NH_RR_STRICT",
                                          "NH_RR_IP",     "NH_RR_LATCHED", "NH_CONSISTENT_HASH"};

NextHopSelectionStrategy::NextHopSelectionStrategy(const std::string_view &name, const NHPolicyType &policy, ts::Yaml::Map &n)
  : strategy_name(name), policy_type(policy)
{
  NH_Debug(NH_DEBUG_TAG, "NextHopSelectionStrategy calling constructor");
  NH_Debug(NH_DEBUG_TAG, "Using a selection strategy of type %s", policy_strings[policy]);

  std::string self_host;
  bool self_host_used = false;

  try {
    // scheme is optional, and strategies with no scheme will match hosts with no scheme
    if (n["scheme"]) {
      auto scheme_val = n["scheme"].Scalar();
      if (scheme_val == "http") {
        scheme = NH_SCHEME_HTTP;
      } else if (scheme_val == "https") {
        scheme = NH_SCHEME_HTTPS;
      } else {
        NH_Note("Invalid scheme '%s' for strategy '%s', setting to NONE", scheme_val.c_str(), strategy_name.c_str());
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
    YAML::Node failover_node_n = n["failover"];
    if (failover_node_n) {
      ts::Yaml::Map failover_node{failover_node_n};
      if (failover_node["ring_mode"]) {
        auto ring_mode_val = failover_node["ring_mode"].Scalar();
        if (ring_mode_val == alternate_rings) {
          ring_mode = NH_ALTERNATE_RING;
        } else if (ring_mode_val == exhaust_rings) {
          ring_mode = NH_EXHAUST_RING;
        } else if (ring_mode_val == peering_rings) {
          ring_mode            = NH_PEERING_RING;
          YAML::Node self_node = failover_node["self"];
          if (self_node) {
            self_host = self_node.Scalar();
            NH_Debug(NH_DEBUG_TAG, "%s is self", self_host.c_str());
          }
        } else {
          ring_mode = NH_ALTERNATE_RING;
          NH_Note("Invalid 'ring_mode' value, '%s', for the strategy named '%s', using default '%s'.", ring_mode_val.c_str(),
                  strategy_name.c_str(), alternate_rings.data());
        }
      }
      if (failover_node["max_simple_retries"]) {
        max_simple_retries = failover_node["max_simple_retries"].as<int>();
      }
      if (failover_node["max_unavailable_retries"]) {
        max_unavailable_retries = failover_node["max_unavailable_retries"].as<int>();
      }

      // response codes for simple retry.
      YAML::Node resp_codes_node;
      if (failover_node["response_codes"]) {
        resp_codes_node = failover_node["response_codes"];
        if (resp_codes_node.Type() != YAML::NodeType::Sequence) {
          NH_Error("Error in the response_codes definition for the strategy named '%s', skipping response_codes.",
                   strategy_name.c_str());
        } else {
          for (auto &&k : resp_codes_node) {
            auto code = k.as<int>();
            if (code > 300 && code < 599) {
              resp_codes.add(code);
            } else {
              NH_Note("Skipping invalid response code '%d' for the strategy named '%s'.", code, strategy_name.c_str());
            }
          }
          resp_codes.sort();
        }
      }
      YAML::Node markdown_codes_node;
      if (failover_node["markdown_codes"]) {
        markdown_codes_node = failover_node["markdown_codes"];
        if (markdown_codes_node.Type() != YAML::NodeType::Sequence) {
          NH_Error("Error in the markdown_codes definition for the strategy named '%s', skipping markdown_codes.",
                   strategy_name.c_str());
        } else {
          for (auto &&k : markdown_codes_node) {
            auto code = k.as<int>();
            if (code > 300 && code < 599) {
              markdown_codes.add(code);
            } else {
              NH_Note("Skipping invalid markdown response code '%d' for the strategy named '%s'.", code, strategy_name.c_str());
            }
          }
          markdown_codes.sort();
        }
      }
      YAML::Node health_check_node;
      if (failover_node["health_check"]) {
        health_check_node = failover_node["health_check"];
        if (health_check_node.Type() != YAML::NodeType::Sequence) {
          NH_Error("Error in the health_check definition for the strategy named '%s', skipping health_checks.",
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
      failover_node.done();
    }

    // parse and load the host data
    YAML::Node groups_node = n["groups"];
    if (groups_node) {
      // a groups list is required.
      if (groups_node.Type() != YAML::NodeType::Sequence) {
        throw std::invalid_argument("Invalid groups definition, expected a sequence, '" + strategy_name + "' cannot be loaded.");
      } else {
        Machine *mach      = Machine::instance();
        HostStatus &h_stat = HostStatus::instance();
        uint32_t grp_size  = groups_node.size();
        if (grp_size > MAX_GROUP_RINGS) {
          NH_Note("the groups list exceeds the maximum of %d for the strategy '%s'. Only the first %d groups will be configured.",
                  MAX_GROUP_RINGS, strategy_name.c_str(), MAX_GROUP_RINGS);
          groups = MAX_GROUP_RINGS;
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
            std::vector<std::shared_ptr<HostRecord>> hosts_inner;

            for (unsigned int hst = 0; hst < hosts_list.size(); ++hst) {
              std::shared_ptr<HostRecord> host_rec = std::make_shared<HostRecord>(hosts_list[hst].as<HostRecordCfg>());
              host_rec->group_index                = grp;
              host_rec->host_index                 = hst;
              if ((self_host == host_rec->hostname) || mach->is_self(host_rec->hostname.c_str())) {
                if (ring_mode == NH_PEERING_RING && grp != 0) {
                  throw std::invalid_argument("self host (" + self_host +
                                              ") can only appear in first host group for peering ring mode");
                }
                h_stat.setHostStatus(host_rec->hostname.c_str(), TSHostStatus::TS_HOST_STATUS_DOWN, 0, Reason::SELF_DETECT);
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
    throw std::invalid_argument("Error parsing the strategy named '" + strategy_name + "' due to '" + ex.what() +
                                "', this strategy will be ignored.");
  }

  if (ring_mode == NH_PEERING_RING) {
    if (groups == 1) {
      if (!go_direct) {
        throw std::invalid_argument("ring mode '" + std::string(peering_rings) +
                                    "' go_direct must be true when there is only one host group");
      }
    } else if (groups != 2) {
      throw std::invalid_argument(
        "ring mode '" + std::string(peering_rings) +
        "' requires two host groups (peering group and an upstream group), or a single peering group with go_direct");
    }
    if (policy_type != NH_CONSISTENT_HASH) {
      throw std::invalid_argument("ring mode '" + std::string(peering_rings) +
                                  "' is only implemented for a 'consistent_hash' policy");
    }
  }
}

void
NextHopSelectionStrategy::markNextHop(TSHttpTxn txnp, const char *hostname, const int port, const NHCmd status, void *ih,
                                      const time_t now)
{
  return passive_health.markNextHop(txnp, hostname, port, status, ih, now);
}

bool
NextHopSelectionStrategy::nextHopExists(TSHttpTxn txnp, void *ih)
{
  HttpSM *sm    = reinterpret_cast<HttpSM *>(txnp);
  int64_t sm_id = sm->sm_id;

  for (uint32_t gg = 0; gg < groups; gg++) {
    for (auto &hh : host_groups[gg]) {
      HostRecord *p = hh.get();
      if (p->available.load()) {
        NH_Debug(NH_DEBUG_TAG,
                 "[%" PRIu64 "] found available next hop %s (this is NOT necessarily the parent which will be selected, just the "
                 "first available parent found)",
                 sm_id, p->hostname.c_str());
        return true;
      }
    }
  }
  return false;
}

ParentRetry_t
NextHopSelectionStrategy::responseIsRetryable(int64_t sm_id, HttpTransact::CurrentInfo &current_info, HTTPStatus response_code)
{
  unsigned sa = current_info.simple_retry_attempts;
  unsigned ua = current_info.unavailable_server_retry_attempts;

  NH_Debug(NH_DEBUG_TAG,
           "[%" PRIu64 "] response_code %d, simple_retry_attempts: %d max_simple_retries: %d, unavailable_server_retry_attempts: "
           "%d, max_unavailable_retries: %d",
           sm_id, response_code, sa, this->max_simple_retries, ua, max_unavailable_retries);
  if (this->resp_codes.contains(response_code) && sa < this->max_simple_retries && sa < this->num_parents) {
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] response code %d is retryable, returning PARENT_RETRY_SIMPLE", sm_id, response_code);
    return PARENT_RETRY_SIMPLE;
  }
  if (this->markdown_codes.contains(response_code) && ua < this->max_unavailable_retries && ua < this->num_parents) {
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] response code %d is retryable, returning PARENT_RETRY_UNAVAILABLE_SERVER", sm_id,
             response_code);
    return PARENT_RETRY_UNAVAILABLE_SERVER;
  }
  NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] response code %d is not retryable, returning PARENT_RETRY_NONE", sm_id, response_code);
  return PARENT_RETRY_NONE;
}

namespace YAML
{
template <> struct convert<HostRecordCfg> {
  static bool
  decode(const Node &node, HostRecordCfg &nh)
  {
    ts::Yaml::Map map{node};
    ts::Yaml::Map *mmap{&map};
    std::optional<ts::Yaml::Map> mergeable_map;

    // check for YAML merge tag.
    YAML::Node mergeable_map_n = map["<<"];
    if (mergeable_map_n) {
      mergeable_map.emplace(mergeable_map_n);
      mmap = &mergeable_map.value();
    }

    // lookup the hostname
    if ((*mmap)["host"]) {
      nh.hostname = (*mmap)["host"].Scalar();
    } else {
      throw std::invalid_argument("Invalid host definition, missing host name.");
    }

    // lookup the port numbers supported by this host.
    YAML::Node proto = (*mmap)["protocol"];

    if (proto.Type() != YAML::NodeType::Sequence) {
      throw std::invalid_argument("Invalid host protocol definition, expected a sequence.");
    } else {
      for (auto &&ii : proto) {
        const YAML::Node &protocol_node = ii;
        std::shared_ptr<NHProtocol> pr  = std::make_shared<NHProtocol>(protocol_node.as<NHProtocol>());
        nh.protocols.push_back(std::move(pr));
      }
    }

    // get the host's weight, allowing override of weight in merged map
    YAML::Node weight = map["weight"];
    if (mmap != &map) {
      // weight must always be looked up in the merged map, even if overridden, so it's presence will not
      // cause an exception when mmap->done() is called
      YAML::Node w = (*mmap)["weight"];
      if (!weight) {
        weight = w;
      }
    }
    if (weight) {
      nh.weight = weight.as<float>();
    } else {
      NH_Note("No weight is defined for the host '%s', using default 1.0", nh.hostname.data());
      nh.weight = 1.0;
    }

    // get the host's optional hash_string
    YAML::Node hash{(*mmap)["hash_string"]};
    if (hash) {
      nh.hash_string = hash.Scalar();
    }

    map.done();
    if (mmap != &map) {
      mmap->done();
    }

    return true;
  }
};

template <> struct convert<NHProtocol> {
  static bool
  decode(const Node &node, NHProtocol &nh)
  {
    ts::Yaml::Map map{node};

    // scheme is optional, and strategies with no scheme will match hosts with no scheme
    if (map["scheme"]) {
      const auto scheme_val = map["scheme"].Scalar();
      if (scheme_val == "http") {
        nh.scheme = NH_SCHEME_HTTP;
      } else if (scheme_val == "https") {
        nh.scheme = NH_SCHEME_HTTPS;
      } else {
        NH_Note("Invalid scheme '%s' for protocol, setting to NONE", scheme_val.c_str());
      }
    }
    if (map["port"]) {
      nh.port = map["port"].as<int>();
      if (nh.port <= 0 || nh.port > 65535) {
        throw YAML::ParserException(map["port"].Mark(), "port number must be in (inclusive) range 1 - 65,536");
      }
    } else {
      if (nh.port == 0) {
        throw YAML::ParserException(map["port"].Mark(), "no port defined must be in (inclusive) range 1 - 65,536");
      }
    }
    if (map["health_check_url"]) {
      nh.health_check_url = map["health_check_url"].Scalar();
    }
    map.done();
    return true;
  }
};
}; // namespace YAML
// namespace YAML
