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
#include <string>

#include "JsonRpcConfig.h"

#include "tscore/Diags.h"
#include "tscore/ts_file.h"
#include "records/I_RecCore.h"

#include "rpc/common/JsonRpcApi.h"
#include "rpc/jsonrpc/JsonRpc.h"

namespace
{
static constexpr auto DEFAULT_FILE_NAME{"proxy.config.jsonrpc.filename"};
static constexpr auto TRANSPORT_TYPE_KEY_NAME{"transport_type"};
static constexpr auto RPC_ENABLED_KEY_NAME{"rpc_enabled"};
static constexpr auto TRANSPORT_CONFIG_KEY_NAME{"transport_config"};
} // namespace
namespace rpc::config
{
void
RPCConfig::load(YAML::Node const &params)
{
  try {
    if (auto n = params[TRANSPORT_TYPE_KEY_NAME]) {
      _selectedTransportType = static_cast<TransportType>(n.as<int>());
    } else {
      Warning("%s not present, using default", TRANSPORT_TYPE_KEY_NAME);
    }

    if (auto n = params[RPC_ENABLED_KEY_NAME]) {
      _rpcEnabled = n.as<bool>();
    } else {
      Warning("%s not present, using default", RPC_ENABLED_KEY_NAME);
    }

    if (auto n = params[TRANSPORT_CONFIG_KEY_NAME]) {
      _transportConfig = n;
    } else {
      Warning("%s not present.", TRANSPORT_CONFIG_KEY_NAME);
    }

  } catch (YAML::Exception const &ex) {
    Warning("We found an issue when reading the parameter: %s . Using defaults", ex.what());
  }
}

YAML::Node
RPCConfig::get_transport_config_params() const
{
  return _transportConfig;
}

RPCConfig::TransportType
RPCConfig::get_transport_type() const
{
  return _selectedTransportType;
}

bool
RPCConfig::is_enabled() const
{
  return _rpcEnabled;
}

void
RPCConfig::load_from_file(std::string_view filePath)
{
  std::error_code ec;
  std::string content{ts::file::load(ts::file::path{filePath}, ec)};

  if (ec) {
    Warning("Cannot open the config file: %s - %s", filePath.data(), strerror(ec.value()));
    // if any issue, let's stop and let the default values be used.
    return;
  }

  YAML::Node rootNode;
  try {
    rootNode = YAML::Load(content);

    // read configured parameters.
    this->load(rootNode);
  } catch (std::exception const &ex) {
    Warning("Something happened parsing the content of %s : %s", filePath.data(), ex.what());
    return;
  };
}

} // namespace rpc::config
