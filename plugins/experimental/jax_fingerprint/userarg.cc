/** @file

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

#include "plugin.h"
#include "config.h"
#include "userarg.h"
#include "context_map.h"

namespace
{

char const *
user_arg_type_name(TSUserArgType type)
{
  switch (type) {
  case TS_USER_ARGS_VCONN:
    return "vconn";
  case TS_USER_ARGS_TXN:
    return "txn";
  default:
    return "unknown";
  }
}

// Shared user arg indices for all jax_fingerprint instances. ATS has limited
// slots (~4 per type), so we share one slot per type and use a ContextMap.
int  vconn_user_arg_index = -1;
int  txn_user_arg_index   = -1;
bool vconn_slot_reserved  = false;
bool txn_slot_reserved    = false;

} // anonymous namespace

int
reserve_user_arg(PluginConfig &config)
{
  TSUserArgType type;
  int          *shared_index;
  bool         *reserved_flag;

  if (config.method.type == Method::Type::CONNECTION_BASED) {
    type          = TS_USER_ARGS_VCONN;
    shared_index  = &vconn_user_arg_index;
    reserved_flag = &vconn_slot_reserved;
  } else {
    type          = TS_USER_ARGS_TXN;
    shared_index  = &txn_user_arg_index;
    reserved_flag = &txn_slot_reserved;
  }

  // Only reserve the slot once per type; subsequent calls reuse it.
  if (!*reserved_flag) {
    int ret = TSUserArgIndexReserve(type, PLUGIN_NAME, "shared JAx context map", shared_index);
    if (ret == TS_SUCCESS) {
      *reserved_flag = true;
      Dbg(dbg_ctl, "Reserved shared user_arg slot: type=%s, index=%d", user_arg_type_name(type), *shared_index);
    } else {
      Dbg(dbg_ctl, "Failed to reserve shared user_arg slot: type=%s", user_arg_type_name(type));
      return ret;
    }
  }

  config.user_arg_index = *shared_index;
  Dbg(dbg_ctl, "Using shared user_arg: type=%s, method=%.*s, index=%d", user_arg_type_name(type),
      static_cast<int>(config.method.name.size()), config.method.name.data(), config.user_arg_index);
  return TS_SUCCESS;
}

void
set_user_arg(void *container, PluginConfig &config, JAxContext *ctx)
{
  ContextMap *map = static_cast<ContextMap *>(TSUserArgGet(container, config.user_arg_index));
  if (map == nullptr) {
    map = new ContextMap();
    TSUserArgSet(container, config.user_arg_index, static_cast<void *>(map));
  }
  map->set(config.method.name, ctx);
}

JAxContext *
get_user_arg(void *container, PluginConfig &config)
{
  ContextMap *map = static_cast<ContextMap *>(TSUserArgGet(container, config.user_arg_index));
  if (map == nullptr) {
    return nullptr;
  }
  return map->get(config.method.name);
}

void
cleanup_user_arg(void *container, PluginConfig &config)
{
  ContextMap *map = static_cast<ContextMap *>(TSUserArgGet(container, config.user_arg_index));
  if (map != nullptr) {
    // Remove this plugin's context from the map.
    map->remove(config.method.name);

    // If the map is now empty, delete it and clear the user arg.
    if (map->empty()) {
      delete map;
      TSUserArgSet(container, config.user_arg_index, nullptr);
    }
  }
}
