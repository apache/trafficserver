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

#include <yaml-cpp/yaml.h>
#include "I_Machine.h"
#include "NextHopSelectionStrategy.h"

// ring mode strings
constexpr std::string_view alternate_rings = "alternate_ring";
constexpr std::string_view exhaust_rings   = "exhaust_ring";

// health check strings
constexpr std::string_view active_health_check  = "active";
constexpr std::string_view passive_health_check = "passive";

constexpr const char *policy_strings[] = {"NH_UNDEFINED", "NH_FIRST_LIVE", "NH_RR_STRICT",
                                          "NH_RR_IP",     "NH_RR_LATCHED", "NH_CONSISTENT_HASH"};

NextHopSelectionStrategy::NextHopSelectionStrategy(const std::string_view &name, const NHPolicyType &policy)
{
  strategy_name = name;
  policy_type   = policy;
  NH_Debug(NH_DEBUG_TAG, "Using a selection strategy of type %s", policy_strings[policy]);
}

//
// parse out the data for this strategy.
//
bool
NextHopSelectionStrategy::Init(const YAML::Node &n)
{
  NH_Debug(NH_DEBUG_TAG, "calling Init()");

  try {
    if (n["scheme"]) {
      auto scheme_val = n["scheme"].Scalar();
      if (scheme_val == "http") {
        scheme = NH_SCHEME_HTTP;
      } else if (scheme_val == "https") {
        scheme = NH_SCHEME_HTTPS;
      } else {
        scheme = NH_SCHEME_NONE;
        NH_Note("Invalid 'scheme' value, '%s', for the strategy named '%s', setting to NH_SCHEME_NONE", scheme_val.c_str(),
                strategy_name.c_str());
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

    // failover node.
    YAML::Node failover_node;
    if (n["failover"]) {
      failover_node = n["failover"];
      if (failover_node["ring_mode"]) {
        auto ring_mode_val = failover_node["ring_mode"].Scalar();
        if (ring_mode_val == alternate_rings) {
          ring_mode = NH_ALTERNATE_RING;
        } else if (ring_mode_val == exhaust_rings) {
          ring_mode = NH_EXHAUST_RING;
        } else {
          ring_mode = NH_ALTERNATE_RING;
          NH_Note("Invalid 'ring_mode' value, '%s', for the strategy named '%s', using default '%s'.", ring_mode_val.c_str(),
                  strategy_name.c_str(), alternate_rings.data());
        }
      }
      if (failover_node["max_simple_retries"]) {
        max_simple_retries = failover_node["max_simple_retries"].as<int>();
      }

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
    }

    // parse and load the host data
    YAML::Node groups_node;
    if (n["groups"]) {
      groups_node = n["groups"];
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
              std::shared_ptr<HostRecord> host_rec = std::make_shared<HostRecord>(hosts_list[hst].as<HostRecord>());
              host_rec->group_index                = grp;
              host_rec->host_index                 = hst;
              if (mach->is_self(host_rec->hostname.c_str())) {
                h_stat.setHostStatus(host_rec->hostname.c_str(), HostStatus_t::HOST_STATUS_DOWN, 0, Reason::SELF_DETECT);
              }
              hosts_inner.push_back(std::move(host_rec));
              num_parents++;
            }
            host_groups.push_back(std::move(hosts_inner));
          }
        }
      }
    }
  } catch (std::exception &ex) {
    NH_Note("Error parsing the strategy named '%s' due to '%s', this strategy will be ignored.", strategy_name.c_str(), ex.what());
    return false;
  }

  return true;
}

void
NextHopSelectionStrategy::markNextHopDown(const uint64_t sm_id, ParentResult &result, const uint64_t fail_threshold,
                                          const uint64_t retry_time, time_t now)
{
  time_t _now;
  now == 0 ? _now = time(nullptr) : _now = now;
  uint32_t new_fail_count                = 0;

  //  Make sure that we are being called back with with a
  //  result structure with a selected parent.
  if (result.result != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result.is_api_result()) {
    ink_assert(0);
    return;
  }
  uint32_t hst_size = host_groups[result.last_group].size();
  ink_assert(result.last_parent < hst_size);
  std::shared_ptr<HostRecord> h = host_groups[result.last_group][result.last_parent];

  // If the parent has already been marked down, just increment
  //   the failure count.  If this is the first mark down on a
  //   parent we need to both set the failure time and set
  //   count to one. If this was the result of a retry, we
  //   must update move the failedAt timestamp to now so that we
  //   continue negative cache the parent
  if (h->failedAt == 0 || result.retry == true) {
    { // start of lock_guard scope.
      std::lock_guard<std::mutex> lock(h->_mutex);
      if (h->failedAt == 0) {
        // Mark the parent failure time.
        h->failedAt = _now;
        if (result.retry == false) {
          new_fail_count = h->failCount = 1;
        }
      } else if (result.retry == true) {
        h->failedAt = _now;
      }
    } // end of lock_guard scope
    NH_Note("[%" PRIu64 "] NextHop %s marked as down %s:%d", sm_id, (result.retry) ? "retry" : "initially", h->hostname.c_str(),
            h->getPort(scheme));

  } else {
    int old_count = 0;

    // if the last failure was outside the retry window, set the failcount to 1 and failedAt to now.
    { // start of lock_guard_scope
      std::lock_guard<std::mutex> lock(h->_mutex);
      if ((h->failedAt + retry_time) < static_cast<unsigned>(_now)) {
        h->failCount = 1;
        h->failedAt  = _now;
      } else {
        old_count = h->failCount = 1;
      }
      new_fail_count = old_count + 1;
    } // end of lock_guard
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] Parent fail count increased to %d for %s:%d", sm_id, new_fail_count, h->hostname.c_str(),
             h->getPort(scheme));
  }

  if (new_fail_count >= fail_threshold) {
    h->set_unavailable();
    NH_Note("[%" PRIu64 "] Failure threshold met failcount:%d >= threshold:%" PRIu64 ", http parent proxy %s:%d marked down", sm_id,
            new_fail_count, fail_threshold, h->hostname.c_str(), h->getPort(scheme));
    NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] NextHop %s:%d marked unavailable, h->available=%s", sm_id, h->hostname.c_str(),
             h->getPort(scheme), (h->available) ? "true" : "false");
  }
}

void
NextHopSelectionStrategy::markNextHopUp(const uint64_t sm_id, ParentResult &result)
{
  //  Make sure that we are being called back with with a
  //   result structure with a parent that is being retried
  ink_assert(result.retry == true);
  if (result.result != PARENT_SPECIFIED) {
    return;
  }
  // If we were set through the API we currently have not failover
  //   so just return fail
  if (result.is_api_result()) {
    ink_assert(0);
    return;
  }
  uint32_t hst_size = host_groups[result.last_group].size();
  ink_assert(result.last_parent < hst_size);
  std::shared_ptr<HostRecord> h = host_groups[result.last_group][result.last_parent];

  if (!h->available) {
    h->set_available();
    NH_Note("[%" PRIu64 "] http parent proxy %s:%d restored", sm_id, h->hostname.c_str(), h->getPort(scheme));
  }
}

bool
NextHopSelectionStrategy::nextHopExists(const uint64_t sm_id)
{
  for (uint32_t gg = 0; gg < groups; gg++) {
    for (auto &hh : host_groups[gg]) {
      HostRecord *p = hh.get();
      if (p->available) {
        NH_Debug(NH_DEBUG_TAG, "[%" PRIu64 "] found available next hop %s", sm_id, p->hostname.c_str());
        return true;
      }
    }
  }
  return false;
}

namespace YAML
{
template <> struct convert<HostRecord> {
  static bool
  decode(const Node &node, HostRecord &nh)
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
        const YAML::Node &protocol_node = ii;
        std::shared_ptr<NHProtocol> pr  = std::make_shared<NHProtocol>(protocol_node.as<NHProtocol>());
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
      NH_Note("No weight is defined for the host '%s', using default 1.0", nh.hostname.data());
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

template <> struct convert<NHProtocol> {
  static bool
  decode(const Node &node, NHProtocol &nh)
  {
    if (node["scheme"]) {
      if (node["scheme"].Scalar() == "http") {
        nh.scheme = NH_SCHEME_HTTP;
      } else if (node["scheme"].Scalar() == "https") {
        nh.scheme = NH_SCHEME_HTTPS;
      } else {
        nh.scheme = NH_SCHEME_NONE;
      }
    }
    if (node["port"]) {
      nh.port = node["port"].as<int>();
    }
    if (node["health_check_url"]) {
      nh.health_check_url = node["health_check_url"].Scalar();
    }
    return true;
  }
};
}; // namespace YAML
// namespace YAML
