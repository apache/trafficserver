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
#pragma once

#include <string_view>

#include "yaml-cpp/yaml.h"

namespace rpc::config
{
///
/// @brief This class holds and parse all the configuration needed to run the JSONRPC server, communication implementation
/// can use this class to feed their own configuration, though it's not mandatory as their API @see
/// BaseCommInterface::configure uses a YAML::Node this class can be used on top of it and parse the "comm_config" from a
/// wider file.
///
/// The configuration is divided into two
/// sections:
/// a) General RPC configuration:
///   "communication_type" Defines the communication that should be used by the server. @see Commype
///   "rpc_enabled" Used to set the toggle to disable or enable the whole server.
///
/// b) Comm specfics Configuration.
///   "comm_config"
///   This is defined by the specific communication, each communication can define and implement their own configuration flags. @see
///   IPCSocketServer::Config for an example
///
/// Example configuration:
///
// rpc:
//   enabled: true
//   unix:
//     lock_path_name: /tmp/conf_jsonrp
//     sock_path_name: /tmp/conf_jsonrpc.sock
//     backlog: 5
//     max_retry_on_transient_errors: 64
///
/// All communication section should use a root node name "comm_config", @c RPCConfig will return the full node when requested @see
/// get_comm_config_param, then it's up to the communication implementation to parse it.
/// @note By default Unix Domain Socket will be used as a communication.
/// @note By default the enable/disable toggle will set to Enabled.
/// @note By default a comm_config node will be Null.
class RPCConfig
{
public:
  enum class CommType { UNIX = 1 };

  RPCConfig() = default;

  /// @brief Get the configured specifics for a particular tansport, all nodes under "comm_config" will be return here.
  //  it's up to the caller to know how to parse this.
  /// @return A YAML::Node that contains the passed configuration.
  YAML::Node get_comm_config_params() const;

  /// @brief Function that returns the configured communication type.
  /// @return a communication type, CommType::UNIX by default.
  CommType get_comm_type() const;

  /// @brief Checks if the server was configured to be enabled or disabled. The server should be explicitly disabled by
  ///        configuration as it is enabled by default.
  /// @return true if enable, false if set disabled.
  bool is_enabled() const;

  /// @brief Load the configuration from the content of a file. If the file does not exist, the default values will be used.
  void load_from_file(std::string const &filePath);

  /// @brief Load configuration from a YAML::Node. This can be used to expose it as public rrc handler.
  void load(YAML::Node const &params);

private:
  YAML::Node _commConfig;                     //!< "comm_config" section of the configuration file.
  CommType _selectedCommType{CommType::UNIX}; //!< The selected (by configuration) communication type. 1 by default.
  bool _rpcEnabled{true};                     //!< holds the configuration toggle value for "rpc_enable" node. Enabled by default.
};
} // namespace rpc::config
