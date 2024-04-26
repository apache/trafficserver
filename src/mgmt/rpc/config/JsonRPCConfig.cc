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

#include "swoc/swoc_file.h"

#include "mgmt/rpc/config/JsonRPCConfig.h"

#include "tscore/Diags.h"
#include "records/RecCore.h"

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"

namespace
{
static constexpr auto RPC_ENABLED_KEY_NAME{"enabled"};
static constexpr auto COMM_CONFIG_KEY_UNIX{"unix"};
} // namespace
namespace rpc::config
{
void
RPCConfig::load(YAML::Node const &params)
{
  try {
    if (auto n = params[RPC_ENABLED_KEY_NAME]) {
      _rpcEnabled = n.as<bool>();
    } else {
      Warning("%s not present.", RPC_ENABLED_KEY_NAME);
    }

    if (auto n = params[COMM_CONFIG_KEY_UNIX]) {
      _commConfig       = n;
      _selectedCommType = CommType::UNIX;
    } else {
      Note("%s not present.", COMM_CONFIG_KEY_UNIX);
    }

  } catch (YAML::Exception const &ex) {
    Warning("We found an issue when reading the parameter: %s . Using defaults", ex.what());
  }
}

YAML::Node
RPCConfig::get_comm_config_params() const
{
  return _commConfig;
}

RPCConfig::CommType
RPCConfig::get_comm_type() const
{
  return _selectedCommType;
}

bool
RPCConfig::is_enabled() const
{
  return _rpcEnabled;
}

void
RPCConfig::load_from_file(std::string const &filePath)
{
  std::error_code ec;
  std::string     content{swoc::file::load(swoc::file::path{filePath}, ec)};

  if (ec) {
    Warning("Cannot open the config file: %s - %s", filePath.c_str(), strerror(ec.value()));
    // The rpc will be enabled by default with the default values.
    return;
  }

  YAML::Node rootNode;
  try {
    rootNode = YAML::Load(content);

    // read configured parameters.
    if (auto rpc = rootNode["rpc"]) {
      this->load(rpc);
    }
  } catch (std::exception const &ex) {
    Warning("Something happened parsing the content of %s : %s", filePath.c_str(), ex.what());
    return;
  };
}

} // namespace rpc::config
