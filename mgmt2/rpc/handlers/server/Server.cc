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

#include "Server.h"

#include "P_Cache.h"
#include <tscore/TSSystemState.h>
#include "rpc/handlers/common/ErrorId.h"
#include "rpc/handlers/common/Utils.h"

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
namespace err                  = rpc::handlers::errors;
static constexpr auto ERROR_ID = err::ID::Server;

static bool
is_server_draining()
{
  RecInt draining = 0;
  if (RecGetRecordInt("proxy.node.config.draining", &draining) != REC_ERR_OKAY) {
    return false;
  }
  return draining != 0;
}

static void inline set_server_drain(bool drain)
{
  TSSystemState::drain(drain);
  RecSetRecordInt("proxy.node.config.draining", TSSystemState::is_draining() ? 1 : 0, REC_SOURCE_DEFAULT);
}

ts::Rv<YAML::Node>
server_start_drain(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;
  try {
    if (!params.IsNull()) {
      DrainInfo di = params.as<DrainInfo>();
      Debug("rpc.server", "draining - No new connections %s", (di.noNewConnections ? "yes" : "no"));
      // TODO: no new connections flag -  implement with the right metric / unimplemented in traffic_ctl
    }

    if (!is_server_draining()) {
      set_server_drain(true);
    } else {
      resp.errata().push(err::to_integral(ERROR_ID), 1, "Server already draining.");
    }
  } catch (std::exception const &ex) {
    Debug("rpc.handler.server", "Got an error DrainInfo decoding: %s", ex.what());
    std::string text;
    ts::bwprint(text, "Error found during server drain: {}", ex.what());
    resp.errata().push(err::to_integral(ERROR_ID), 1, text);
  }
  return resp;
}

ts::Rv<YAML::Node>
server_stop_drain(std::string_view const &id, [[maybe_unused]] YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;
  if (is_server_draining()) {
    set_server_drain(false);
  } else {
    resp.errata().push(err::to_integral(ERROR_ID), 1, "Server is not draining.");
  }

  return resp;
}
} // namespace rpc::handlers::server
