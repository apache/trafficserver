/** @file

  Config reload error codes — shared between server (Configuration.cc) and client (CtrlCommands.cc).

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

namespace config::reload::errors
{
/// Error codes for config reload RPC operations.
/// Used in the YAML error nodes exchanged between traffic_server and traffic_ctl.
///
/// Range 6001–6099: general reload lifecycle errors
/// Range 6010–6019: per-config validation errors
enum class ConfigReloadError : int {
  // --- General reload errors ---
  TOKEN_NOT_FOUND      = 6001, ///< Requested token does not exist in history
  TOKEN_ALREADY_EXISTS = 6002, ///< Token name already in use
  RELOAD_TASK_FAILED   = 6003, ///< Failed to create or kick off reload task
  RELOAD_IN_PROGRESS   = 6004, ///< A reload is already running (use --force to override)
  NO_RELOAD_TASKS      = 6005, ///< No reload tasks found in history

  // --- Per-config validation errors ---
  CONFIG_NOT_REGISTERED    = 6010, ///< Config key not found in ConfigRegistry
  RPC_SOURCE_NOT_SUPPORTED = 6011, ///< Config does not support RPC as a content source
  CONFIG_NO_HANDLER        = 6012, ///< Config is registered but has no reload handler
};

/// Helper to convert enum to int for YAML node construction
constexpr int
to_int(ConfigReloadError e)
{
  return static_cast<int>(e);
}
} // namespace config::reload::errors
