/**
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

#include "../../../../iocore/cache/P_CacheDir.h"
#include "iocore/eventsystem/EventProcessor.h"
#include "iocore/net/ConnectionTracker.h"
#include "mgmt/rpc/handlers/server/Server.h"
#include "mgmt/rpc/handlers/common/ErrorUtils.h"
#include "mgmt/rpc/handlers/common/Utils.h"
#include "tscore/TSSystemState.h"
#include "tsutil/Metrics.h"

namespace
{
DbgCtl dbg_ctl_rpc_server{"rpc.server"};
DbgCtl dbg_ctl_rpc_handler_server{"rpc.handler.server"};

} // end anonymous namespace

namespace rpc::handlers::server
{
namespace field_names
{
  static constexpr auto NEW_CONNECTIONS{"no_new_connections"};
} // namespace field_names

struct DrainInfo {
  bool noNewConnections{false};
};

struct ConnectionTrackingInfo {
  enum TableFlags {
    NOT_SET  = 0,
    INBOUND  = 1 << 0, // Inbound table
    OUTBOUND = 1 << 1  // Outbound table
  };
  TableFlags table{TableFlags::OUTBOUND}; // default.
};
using TFLags = ConnectionTrackingInfo::TableFlags;
constexpr TFLags
operator|(const TFLags rhs, const TFLags lhs)
{
  return static_cast<TFLags>(static_cast<uint32_t>(rhs) | static_cast<uint32_t>(lhs));
}

constexpr TFLags &
operator|=(TFLags &rhs, TFLags lhs)
{
  return rhs = rhs | lhs;
}

constexpr TFLags
operator&(TFLags rhs, TFLags lhs)
{
  return static_cast<TFLags>(static_cast<uint32_t>(rhs) & static_cast<uint32_t>(lhs));
}
} // namespace rpc::handlers::server
namespace YAML
{
template <> struct convert<rpc::handlers::server::DrainInfo> {
  static bool
  decode(const Node &node, rpc::handlers::server::DrainInfo &rhs)
  {
    namespace field = rpc::handlers::server::field_names;
    namespace utils = rpc::handlers::utils;
    if (!node.IsMap()) {
      return false;
    }
    // optional
    if (auto n = node[field::NEW_CONNECTIONS]; utils::is_true_flag(n)) {
      rhs.noNewConnections = true;
    }
    return true;
  }
};
template <> struct convert<rpc::handlers::server::ConnectionTrackingInfo> {
  static constexpr auto table{"table"};
  static bool
  decode(const Node &node, rpc::handlers::server::ConnectionTrackingInfo &rhs)
  {
    namespace field = rpc::handlers::server::field_names;
    if (!node.IsMap()) {
      return false;
    }
    // optional
    if (auto n = node[table]; !n.IsNull()) {
      auto const &val = n.as<std::string>();
      if (val == "both") {
        rhs.table = rpc::handlers::server::TFLags::INBOUND | rpc::handlers::server::TFLags::OUTBOUND;
      } else if (val == "inbound") {
        rhs.table = rpc::handlers::server::TFLags::INBOUND;
      } else if (val == "outbound") {
        rhs.table = rpc::handlers::server::TFLags::OUTBOUND;
      } else {
        throw std::runtime_error("Invalid table type. Use [both|inbound|outbound]");
      }
    }
    return true;
  }
};
} // namespace YAML

namespace rpc::handlers::server
{
namespace err = rpc::handlers::errors;

static bool
is_server_draining()
{
  ts::Metrics &metrics  = ts::Metrics::instance();
  static auto  drain_id = metrics.lookup("proxy.process.proxy.draining");

  return (metrics[drain_id].load() != 0);
}

static void
set_server_drain(bool drain)
{
  ts::Metrics &metrics  = ts::Metrics::instance();
  static auto  drain_id = metrics.lookup("proxy.process.proxy.draining");

  TSSystemState::drain(drain);
  metrics[drain_id].store(TSSystemState::is_draining() ? 1 : 0);
}

swoc::Rv<YAML::Node>
server_start_drain(std::string_view const & /* id ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> resp;
  try {
    if (!params.IsNull()) {
      DrainInfo di = params.as<DrainInfo>();
      Dbg(dbg_ctl_rpc_server, "draining - No new connections %s", (di.noNewConnections ? "yes" : "no"));
      // TODO: no new connections flag -  implement with the right metric / unimplemented in traffic_ctl
    }

    if (!is_server_draining()) {
      set_server_drain(true);
    } else {
      resp.errata().assign(std::error_code{errors::Codes::SERVER}).note("Server already draining.");
    }
  } catch (std::exception const &ex) {
    Dbg(dbg_ctl_rpc_handler_server, "Got an error DrainInfo decoding: %s", ex.what());
    resp.errata().assign(std::error_code{errors::Codes::SERVER}).note("Error found during server drain: {}", ex.what());
  }
  return resp;
}

swoc::Rv<YAML::Node>
server_stop_drain(std::string_view const & /* id ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  swoc::Rv<YAML::Node> resp;
  if (is_server_draining()) {
    set_server_drain(false);
  } else {
    resp.errata().assign(std::error_code{errors::Codes::SERVER}).note("Server is not draining.");
  }

  return resp;
}

void
server_shutdown(YAML::Node const &)
{
  sync_cache_dir_on_shutdown();
}

swoc::Rv<YAML::Node>
get_server_status(std::string_view const & /* params ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  swoc::Rv<YAML::Node> resp;
  try {
    auto bts = [](bool val) -> std::string { return val ? "true" : "false"; };

    YAML::Node data;
    data["initialized_done"]           = bts(!TSSystemState::is_initializing());
    data["is_ssl_handshaking_stopped"] = bts(TSSystemState::is_ssl_handshaking_stopped());
    data["is_draining"]                = bts(TSSystemState::is_draining());
    data["is_event_system_shut_down"]  = bts(TSSystemState::is_event_system_shut_down());

    YAML::Node threads;
    for (const auto &tgs : eventProcessor.thread_group) {
      if (!tgs._name.empty()) {
        YAML::Node grp;
        grp["name"]    = tgs._name;
        grp["count"]   = YAML::Node(tgs._count).Scalar();
        grp["started"] = bts(tgs._started.load());
        threads.push_back(grp);
      }
    }

    data["thread_groups"] = threads;

    resp.result()["data"] = data;

  } catch (std::exception const &ex) {
    resp.errata()
      .assign(std::error_code{errors::Codes::SERVER})
      .note("Error found when calling get_server_status API: {}", ex.what());
  }
  return resp;
}

swoc::Rv<YAML::Node>
get_connection_tracker_info(std::string_view const & /* params ATS_UNUSED */, YAML::Node const &params)
{
  swoc::Rv<YAML::Node> resp;
  try {
    ConnectionTrackingInfo p;
    if (!params.IsNull()) {
      p = params.as<ConnectionTrackingInfo>();
    }

    if (p.table & TFLags::OUTBOUND) {
      std::string json{ConnectionTracker::outbound_to_json_string()};
      resp.result()["outbound"] = YAML::Load(json);
    }

    if (p.table & TFLags::INBOUND) {
      std::string json{ConnectionTracker::inbound_to_json_string()};
      resp.result()["inbound"] = YAML::Load(json);
    }

  } catch (std::exception const &ex) {
    resp.errata()
      .assign(std::error_code{errors::Codes::SERVER})
      .note("Error found when calling get_connection_tracker_info API: {}", ex.what());
  }
  return resp;
}
} // namespace rpc::handlers::server
