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
#include "fingerprint_registry.h"

#include <cstring>

namespace
{

char const *
user_arg_type_name(TSUserArgType type)
{
  switch (type) {
  case TS_USER_ARGS_VCONN:
    return "vconn";
  case TS_USER_ARGS_SSN:
    return "ssn";
  case TS_USER_ARGS_TXN:
    return "txn";
  default:
    return "unknown";
  }
}

int
reserve_slot(TSUserArgType type, PluginConfig const &config, int &index)
{
  const char *name = config.export_name.empty() ? PLUGIN_NAME : config.export_name.c_str();

  const char *description = nullptr;
  if (TSUserArgIndexNameLookup(type, name, &index, &description) == TS_SUCCESS) {
    if (description == nullptr || std::strcmp(description, jax_fingerprint::REGISTRY_DESCRIPTION) != 0) {
      TSError("[%s] User arg '%s' is already reserved with an incompatible data contract", PLUGIN_NAME, name);
      return TS_ERROR;
    }
  } else if (TSUserArgIndexReserve(type, name, jax_fingerprint::REGISTRY_DESCRIPTION, &index) == TS_SUCCESS) {
    Dbg(dbg_ctl, "Reserved shared user_arg slot: type=%s, name=%s, index=%d", user_arg_type_name(type), name, index);
  } else {
    Dbg(dbg_ctl, "Failed to reserve shared user_arg slot: type=%s, name=%s", user_arg_type_name(type), name);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "Using shared user_arg: type=%s, name=%s, method=%.*s, index=%d", user_arg_type_name(type), name,
      static_cast<int>(config.method.name.size()), config.method.name.data(), index);
  return TS_SUCCESS;
}

ContextMap *
get_map(void *container, int index)
{
  auto *registry = static_cast<jax_fingerprint::RegistryV1 *>(TSUserArgGet(container, index));
  return registry == nullptr ? nullptr : static_cast<ContextMap *>(registry);
}

void
set_context(void *container, int index, std::string_view method_name, JAxContext *ctx)
{
  ContextMap *map = get_map(container, index);
  if (map == nullptr) {
    map = new ContextMap();
    TSUserArgSet(container, index, static_cast<jax_fingerprint::RegistryV1 *>(map));
  }
  map->set(method_name, ctx);
}

JAxContext *
get_context(void *container, int index, std::string_view method_name)
{
  ContextMap *map = get_map(container, index);
  return map == nullptr ? nullptr : map->get(method_name);
}

void
cleanup_context(void *container, int index, std::string_view method_name)
{
  ContextMap *map = get_map(container, index);
  if (map != nullptr) {
    // Remove this plugin's context from the map.
    map->remove(method_name);

    // If the map is now empty, delete it and clear the user arg.
    if (map->empty()) {
      delete map;
      TSUserArgSet(container, index, nullptr);
    }
  }
}

} // anonymous namespace

int
reserve_user_arg(PluginConfig &config)
{
  TSUserArgType type = config.method.type == Method::Type::CONNECTION_BASED ? TS_USER_ARGS_VCONN : TS_USER_ARGS_TXN;
  return reserve_slot(type, config, config.user_arg_index);
}

void
set_user_arg(void *container, PluginConfig &config, JAxContext *ctx)
{
  set_context(container, config.user_arg_index, config.method.name, ctx);
}

JAxContext *
get_user_arg(void *container, PluginConfig &config)
{
  return get_context(container, config.user_arg_index, config.method.name);
}

void
refresh_user_arg(void *container, PluginConfig &config)
{
  if (ContextMap *map = get_map(container, config.user_arg_index); map != nullptr) {
    map->refresh(config.method.name);
  }
}

void
cleanup_user_arg(void *container, PluginConfig &config)
{
  cleanup_context(container, config.user_arg_index, config.method.name);
}

int
reserve_session_user_arg(PluginConfig &config)
{
  return reserve_slot(TS_USER_ARGS_SSN, config, config.session_user_arg_index);
}

void
copy_to_session_user_arg(TSHttpSsn ssnp, PluginConfig &config, JAxContext const &ctx, uint64_t txn_id)
{
  auto *copy = new JAxContext(ctx);
  copy->set_txn_id(txn_id);
  set_context(ssnp, config.session_user_arg_index, config.method.name, copy);
}

JAxContext *
get_session_user_arg(TSHttpSsn ssnp, PluginConfig &config)
{
  return get_context(ssnp, config.session_user_arg_index, config.method.name);
}

void
cleanup_session_user_arg(TSHttpSsn ssnp, PluginConfig &config)
{
  cleanup_context(ssnp, config.session_user_arg_index, config.method.name);
}
