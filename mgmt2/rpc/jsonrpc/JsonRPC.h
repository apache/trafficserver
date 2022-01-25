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

#include "JsonRPCManager.h"
#include "rpc/handlers/common/ErrorUtils.h"

namespace rpc
{
/// Generic and global JSONRPC service provider info object. It's recommended to use this object when registring your new handler
/// into the rpc system IF the implementor wants the handler to be listed as ATS's handler.
extern RPCRegistryInfo core_ats_rpc_service_provider_handle;
// -----------------------------------------------------------------------------
/// Set of convenience API function. Users can avoid having to name the whole rps::JsonRPCManager::instance() to use the object.

/// @see JsonRPCManager::add_method_handler for details
template <typename Func>
inline bool
add_method_handler(const std::string &name, Func &&call, const RPCRegistryInfo *info = nullptr)
{
  return JsonRPCManager::instance().add_method_handler(name, std::forward<Func>(call), info);
}

/// @see JsonRPCManager::add_notification_handler for details
template <typename Func>
inline bool
add_notification_handler(const std::string &name, Func &&call, const RPCRegistryInfo *info = nullptr)
{
  return JsonRPCManager::instance().add_notification_handler(name, std::forward<Func>(call), info);
}

} // namespace rpc
