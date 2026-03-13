/* @file
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

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"

namespace rpc::handlers::config
{
/// Only records.yaml config.
swoc::Rv<YAML::Node> set_config_records(std::string_view const &id, YAML::Node const &params);

/**
 * @brief Unified config reload handler — supports file source and RPC source modes.
 *
 * File source (default):
 *   Reloads all changed config files from disk (on-disk configuration).
 *   Params:
 *     token: optional custom reload token
 *     force: force reload even if one is in progress
 *
 * RPC source (when "configs" param present):
 *   Reloads specific configs using YAML content injected through the RPC call,
 *   bypassing on-disk files.
 *   Params:
 *     token: optional custom reload token
 *     configs: map of config_key -> yaml_content
 *       e.g.:
 *         configs:
 *           ip_allow:
 *             - apply: in
 *               ip_addrs: 0.0.0.0/0
 *           sni:
 *             - fqdn: '*.example.com'
 *
 * Response:
 *   token: reload task token
 *   created_time: task creation timestamp
 *   message: status message
 *   errors: array of errors (if any)
 */
swoc::Rv<YAML::Node> reload_config(std::string_view const &id, YAML::Node const &params);

swoc::Rv<YAML::Node> get_reload_config_status(std::string_view const &id, YAML::Node const &params);

} // namespace rpc::handlers::config
