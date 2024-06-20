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

#include "mgmt/rpc/handlers/server/Server.h"

#include "../../../../iocore/cache/P_Cache.h"
#include <tscore/TSSystemState.h>
#include "mgmt/rpc/handlers/common/ErrorUtils.h"
#include "mgmt/rpc/handlers/common/Utils.h"

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
} // namespace rpc::handlers::server
