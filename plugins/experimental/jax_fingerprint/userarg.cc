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
#include <vector>

namespace
{

// All plugin instances share this slot. It is deliberately not named after --export or described with
// REGISTRY_DESCRIPTION, so other plugins do not mistake it for the fingerprint registry.
constexpr char SESSION_ARG_NAME[]        = "jax_fingerprint.log_field";
constexpr char SESSION_ARG_DESCRIPTION[] = "jax_fingerprint private --log-field fingerprint copies";

/** The fingerprints the --log-field callbacks of all plugin instances saved on a session.
 *
 * An entry is keyed by the export registry slot and the method, the same pair that identifies a context, so
 * instances that export to different registries never read each other's copies.
 */
class SessionFingerprints
{
public:
  void
  set(int user_arg_index, std::string_view method_name, std::string_view fingerprint, std::optional<uint64_t> txn_id)
  {
    Entry *entry = find(user_arg_index, method_name);
    if (entry == nullptr) {
      entry                 = &_entries.emplace_back();
      entry->user_arg_index = user_arg_index;
      entry->method_name    = method_name;
    }
    entry->fingerprint = fingerprint;
    entry->txn_id      = txn_id;
  }

  std::string const *
  get(int user_arg_index, std::string_view method_name, std::optional<uint64_t> txn_id) const
  {
    for (auto const &entry : _entries) {
      if (entry.user_arg_index == user_arg_index && entry.method_name == method_name) {
        return entry.txn_id == txn_id ? &entry.fingerprint : nullptr;
      }
    }
    return nullptr;
  }

private:
  struct Entry {
    int                     user_arg_index = -1;
    std::string             method_name;
    std::string             fingerprint;
    std::optional<uint64_t> txn_id;
  };

  Entry *
  find(int user_arg_index, std::string_view method_name)
  {
    for (auto &entry : _entries) {
      if (entry.user_arg_index == user_arg_index && entry.method_name == method_name) {
        return &entry;
      }
    }
    return nullptr;
  }

  std::vector<Entry> _entries;
};

/** The session slot that holds SessionFingerprints, or -1 if no --log-field instance is loaded.
 *
 * Instances without --log-field still save copies, because the context they share with a --log-field
 * instance is freed by whichever TXN_CLOSE hook runs first. Every --log-field instance reserves the slot in
 * TSPluginInit, so the first call, which comes from a session or transaction hook, sees the final answer.
 */
int
session_arg_index()
{
  static int const index = [] {
    int         found       = -1;
    char const *description = nullptr;
    if (TSUserArgIndexNameLookup(TS_USER_ARGS_SSN, SESSION_ARG_NAME, &found, &description) != TS_SUCCESS ||
        description == nullptr || std::strcmp(description, SESSION_ARG_DESCRIPTION) != 0) {
      return -1;
    }
    return found;
  }();
  return index;
}

/** Whether @a ssnp is an HQSession, which carries HTTP/3 or HTTP/0.9 over QUIC or QMux.
 *
 * The session's first protocol tag is the ALPN HQSession negotiated, and every ALPN it accepts ("h3", "h3-29",
 * "h3qx-01", "hq", "hq-29") starts with "h3" or "hq". A QMux session's net connection reports only TLS and
 * TCP, so the tags below the ALPN do not identify it. The quiche ALPN is not NUL-terminated, so only the
 * prefix is compared.
 */
bool
is_hq_session(TSHttpSsn ssnp)
{
  char const *tag   = nullptr;
  int         count = 0;
  if (TSHttpSsnClientProtocolStackGet(ssnp, 1, &tag, &count) != TS_SUCCESS || count < 1 || tag == nullptr) {
    return false;
  }
  return std::strncmp(tag, "h3", 2) == 0 || std::strncmp(tag, "hq", 2) == 0;
}

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
  int        &index       = config.session_user_arg_index;
  const char *description = nullptr;

  if (TSUserArgIndexNameLookup(TS_USER_ARGS_SSN, SESSION_ARG_NAME, &index, &description) == TS_SUCCESS) {
    if (description == nullptr || std::strcmp(description, SESSION_ARG_DESCRIPTION) != 0) {
      TSError("[%s] User arg '%s' is already reserved by another plugin", PLUGIN_NAME, SESSION_ARG_NAME);
      return TS_ERROR;
    }
  } else if (TSUserArgIndexReserve(TS_USER_ARGS_SSN, SESSION_ARG_NAME, SESSION_ARG_DESCRIPTION, &index) != TS_SUCCESS) {
    Dbg(dbg_ctl, "Failed to reserve the session user_arg slot: name=%s", SESSION_ARG_NAME);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "Using session user_arg: name=%s, method=%.*s, index=%d", SESSION_ARG_NAME,
      static_cast<int>(config.method.name.size()), config.method.name.data(), index);
  return TS_SUCCESS;
}

void
save_session_fingerprint(TSHttpSsn ssnp, PluginConfig &config, std::string_view fingerprint, std::optional<uint64_t> txn_id)
{
  int const index = session_arg_index();
  if (index == -1) {
    return;
  }
  auto *copies = static_cast<SessionFingerprints *>(TSUserArgGet(ssnp, index));
  if (copies == nullptr) {
    // HQSession never fires SSN_CLOSE, so nothing would free copies saved on an HTTP/3 or HTTP/0.9 session.
    if (is_hq_session(ssnp)) {
      Dbg(dbg_ctl, "Not saving --log-field session copies on HQ session %p", ssnp);
      return;
    }
    copies = new SessionFingerprints();
    TSUserArgSet(ssnp, index, copies);
    Dbg(dbg_ctl, "Created --log-field session copies for session %p", ssnp);
  }
  copies->set(config.user_arg_index, config.method.name, fingerprint, txn_id);
}

std::string const *
get_session_fingerprint(TSHttpSsn ssnp, PluginConfig &config, std::optional<uint64_t> txn_id)
{
  int const index = session_arg_index();
  if (index == -1) {
    return nullptr;
  }
  auto const *copies = static_cast<SessionFingerprints const *>(TSUserArgGet(ssnp, index));
  return copies == nullptr ? nullptr : copies->get(config.user_arg_index, config.method.name, txn_id);
}

void
cleanup_session_user_arg(TSHttpSsn ssnp)
{
  int const index = session_arg_index();
  if (index == -1) {
    return;
  }
  // Every transaction on the session has been logged by now, so the first SSN_CLOSE hook frees the copies of
  // all instances, including those of instances that do not hook SSN_CLOSE.
  if (auto *copies = static_cast<SessionFingerprints *>(TSUserArgGet(ssnp, index)); copies != nullptr) {
    delete copies;
    TSUserArgSet(ssnp, index, nullptr);
    Dbg(dbg_ctl, "Freed --log-field session copies for session %p", ssnp);
  }
}
