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

#include <mutex>
#include <string_view>
#include <unordered_map>

namespace
{
struct MethodKey {
  std::string_view method_name = {};
  Method::Type     method_type = Method::Type::CONNECTION_BASED;

  bool
  operator==(MethodKey const &that) const
  {
    return method_name == that.method_name && method_type == that.method_type;
  }
};

struct MethodKeyHash {
  size_t
  operator()(MethodKey const &key) const
  {
    auto const method_hash = std::hash<std::string_view>{}(key.method_name);
    auto const type_hash   = std::hash<int>{}(static_cast<int>(key.method_type));
    return method_hash ^ (type_hash + 0x9e3779b97f4a7c15ULL + (method_hash << 6U) + (method_hash >> 2U));
  }
};

using ContextStore = std::unordered_map<MethodKey, JAxContext *, MethodKeyHash>;

std::once_flag vconn_user_arg_once;
std::once_flag txn_user_arg_once;
int            vconn_user_arg_index  = -1;
int            txn_user_arg_index    = -1;
TSReturnCode   vconn_user_arg_status = TS_ERROR;
TSReturnCode   txn_user_arg_status   = TS_ERROR;

MethodKey
make_method_key(PluginConfig const &config)
{
  return {config.method.name, config.method.type};
}

int &
get_slot_index(Method::Type type)
{
  return type == Method::Type::CONNECTION_BASED ? vconn_user_arg_index : txn_user_arg_index;
}

TSReturnCode &
get_slot_status(Method::Type type)
{
  return type == Method::Type::CONNECTION_BASED ? vconn_user_arg_status : txn_user_arg_status;
}

std::once_flag &
get_slot_once(Method::Type type)
{
  return type == Method::Type::CONNECTION_BASED ? vconn_user_arg_once : txn_user_arg_once;
}

TSUserArgType
get_user_arg_type(Method::Type type)
{
  return type == Method::Type::CONNECTION_BASED ? TS_USER_ARGS_VCONN : TS_USER_ARGS_TXN;
}

char const *
get_slot_name(Method::Type type)
{
  return type == Method::Type::CONNECTION_BASED ? "jax_fingerprint_vconn_ctx" : "jax_fingerprint_txn_ctx";
}

TSReturnCode
ensure_slot_reserved(Method::Type type)
{
  std::call_once(get_slot_once(type), [type]() {
    auto &slot_status = get_slot_status(type);
    auto &slot_index  = get_slot_index(type);
    slot_status       = TSUserArgIndexReserve(get_user_arg_type(type), get_slot_name(type),
                                              "used to pass JAx method contexts between hooks", &slot_index);
    Dbg(dbg_ctl, "user_arg_name: %s, user_arg_index: %d", get_slot_name(type), slot_index);
  });
  return get_slot_status(type);
}

ContextStore *
get_context_store(void *container, PluginConfig &config)
{
  if (config.user_arg_index < 0 && reserve_user_arg(config) == TS_ERROR) {
    return nullptr;
  }
  return static_cast<ContextStore *>(TSUserArgGet(container, config.user_arg_index));
}

} // end anonymous namespace

int
reserve_user_arg(PluginConfig &config)
{
  auto const ret = ensure_slot_reserved(config.method.type);
  if (ret == TS_SUCCESS) {
    config.user_arg_index = get_slot_index(config.method.type);
  }
  return ret;
}

void
set_user_arg(void *container, PluginConfig &config, JAxContext *ctx)
{
  if (config.user_arg_index < 0 && reserve_user_arg(config) == TS_ERROR) {
    return;
  }

  auto const method_key = make_method_key(config);
  auto      *store      = static_cast<ContextStore *>(TSUserArgGet(container, config.user_arg_index));

  if (ctx == nullptr) {
    if (store == nullptr) {
      return;
    }
    store->erase(method_key);
    if (store->empty()) {
      delete store;
      TSUserArgSet(container, config.user_arg_index, nullptr);
    }
    return;
  }

  if (store == nullptr) {
    store = new ContextStore();
    TSUserArgSet(container, config.user_arg_index, store);
  }
  (*store)[method_key] = ctx;
}

JAxContext *
get_user_arg(void *container, PluginConfig &config)
{
  auto *store = get_context_store(container, config);
  if (store == nullptr) {
    return nullptr;
  }

  auto const it = store->find(make_method_key(config));
  return it == store->end() ? nullptr : it->second;
}
