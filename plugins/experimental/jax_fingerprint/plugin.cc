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
#include "context.h"
#include "userarg.h"
#include "method.h"
#include "header.h"
#include "log.h"

#include <ts/apidefs.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/remap_version.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <version>

DbgCtl dbg_ctl{PLUGIN_NAME};

namespace
{
bool
prepare_config(PluginConfig &config)
{
  if (!config.log_filename.empty()) {
    if (!create_log_file(config.log_filename, config.log_handle)) {
      TSError("[%s] Failed to create log.", PLUGIN_NAME);
      return false;
    }
    Dbg(dbg_ctl, "Created log file.");
  }

  if (reserve_user_arg(config) == TS_ERROR) {
    TSError("[%s] Failed to reserve user arg index.", PLUGIN_NAME);
    return false;
  }

  return true;
}

/** Prepare every configuration, releasing any log file already created if one of them fails. */
bool
prepare_configs(PluginConfigs &configs)
{
  for (auto const &config : configs) {
    if (!prepare_config(*config)) {
      for (auto const &created : configs) {
        if (created->log_handle != nullptr) {
          flush_log_file(created->log_handle);
          created->log_handle = nullptr;
        }
      }
      return false;
    }
  }
  return true;
}

void
register_log_field(PluginConfig *config)
{
  std::string name  = "jax_fingerprint-";
  name             += config->method.name;
  TSLogFieldRegister(
    name.c_str(), config->log_symbol, TS_LOG_TYPE_STRING,
    [config](TSHttpTxn txnp, char *buf) -> int {
      void *container;
      if (config->method.type == Method::Type::CONNECTION_BASED) {
        container = TSHttpSsnClientVConnGet(TSHttpTxnSsnGet(txnp));
      } else {
        container = txnp;
      }
      JAxContext *ctx = get_user_arg(container, *config);
      if (ctx) {
        return TSLogStringMarshal(buf, ctx->get_fingerprint());
      } else {
        return TSLogStringMarshal(buf, "-");
      }
    },
    TSLogIntUnmarshal);
}
} // namespace

void
modify_headers(JAxContext *ctx, TSHttpTxn txnp, PluginConfig &config)
{
  if (!ctx->get_fingerprint().empty()) {
    switch (config.mode) {
    case Mode::KEEP:
      if (!config.header_name.empty() && !has_header(txnp, config.header_name)) {
        set_header(txnp, config.header_name, ctx->get_fingerprint());
      }
      if (!config.via_header_name.empty() && !has_header(txnp, config.via_header_name)) {
        set_via_header(txnp, config.via_header_name);
      }
      break;
    case Mode::OVERWRITE:
      if (!config.header_name.empty()) {
        set_header(txnp, config.header_name, ctx->get_fingerprint());
      }
      if (!config.via_header_name.empty()) {
        set_via_header(txnp, config.via_header_name);
      }
      break;
    case Mode::APPEND:
      if (!config.header_name.empty()) {
        append_header(txnp, config.header_name, ctx->get_fingerprint());
      }
      if (!config.via_header_name.empty()) {
        append_via_header(txnp, config.via_header_name);
      }
      break;
    default:
      break;
    }
  } else {
    Dbg(dbg_ctl, "No fingerprint attached to vconn!");
    if (config.mode == Mode::OVERWRITE) {
      if (!config.header_name.empty()) {
        remove_header(txnp, config.header_name);
      }
      if (!config.via_header_name.empty()) {
        remove_header(txnp, config.via_header_name);
      }
    }
  }
}

int
handle_client_hello(void *edata, PluginConfig &config)
{
  TSVConn     vconn = static_cast<TSVConn>(edata);
  JAxContext *ctx   = get_user_arg(vconn, config);

  if (!config.servernames.empty()) {
    const char *servername;
    int         servername_len;
    servername = TSVConnSslSniGet(vconn, &servername_len);
    if (servername != nullptr && servername_len > 0) {
#ifdef __cpp_lib_generic_unordered_lookup
      if (!config.servernames.contains(std::string_view(servername, servername_len))) {
#else
      if (!config.servernames.contains({servername, static_cast<size_t>(servername_len)})) {
#endif
        Dbg(dbg_ctl, "Server name %.*s is not in the server name set", servername_len, servername);
        TSVConnReenable(vconn);
        return TS_SUCCESS;
      }
    } else {
      Dbg(dbg_ctl, "No SNI present but server name filtering is configured; skipping fingerprint generation");
      TSVConnReenable(vconn);
      return TS_SUCCESS;
    }
  }

  if (nullptr == ctx) {
    ctx = new JAxContext(config.method.name.data(), TSNetVConnRemoteAddrGet(vconn));
    set_user_arg(vconn, config, ctx);
  }

  if (config.method.on_client_hello) {
    config.method.on_client_hello(ctx, vconn);
    refresh_user_arg(vconn, config);
  }

  TSVConnReenable(vconn);

  return TS_SUCCESS;
}

int
handle_read_request_hdr(void *edata, PluginConfig &config)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
  if (ssnp == nullptr) {
    Dbg(dbg_ctl, "Failed to get ssn object.");
    return TS_SUCCESS;
  }

  TSVConn vconn = TSHttpSsnClientVConnGet(ssnp);
  if (vconn == nullptr) {
    Dbg(dbg_ctl, "Failed to get vconn object.");
    return TS_SUCCESS;
  }

  void *container;
  if (config.method.type == Method::Type::CONNECTION_BASED) {
    container = vconn;
  } else {
    container = txnp;
  }
  JAxContext *ctx = get_user_arg(container, config);
  if (nullptr == ctx) {
    if (container == vconn) {
      // JAxContext should be created on client hello hook
      Dbg(dbg_ctl, "No context. Skipping.");
      return TS_SUCCESS;
    }
    ctx = new JAxContext(config.method.name.data(), TSNetVConnRemoteAddrGet(vconn));
    set_user_arg(container, config, ctx);
  }

  if (config.method.on_request) {
    config.method.on_request(ctx, txnp);
    refresh_user_arg(container, config);
  }

  if (!config.log_filename.empty()) {
    log_fingerprint(ctx, config.log_handle);
  }

  modify_headers(ctx, txnp, config);

  return TS_SUCCESS;
}

int
handle_http_txn_close(void *edata, PluginConfig &config)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  cleanup_user_arg(txnp, config);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_vconn_close(void *edata, PluginConfig &config)
{
  TSVConn vconn = static_cast<TSVConn>(edata);

  cleanup_user_arg(vconn, config);

  TSVConnReenable(vconn);
  return TS_SUCCESS;
}

int
main_handler(TSCont cont, TSEvent event, void *edata)
{
  int ret;

  auto config = static_cast<PluginConfig *>(TSContDataGet(cont));
  if (config == nullptr) {
    // Null config means this continuation is no longer needed.
    if (event == TS_EVENT_SSL_CLIENT_HELLO || event == TS_EVENT_VCONN_CLOSE) {
      TSVConnReenable(static_cast<TSVConn>(edata));
    } else {
      TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    }
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO:
    ret = handle_client_hello(edata, *config);
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    ret = handle_read_request_hdr(edata, *config);
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    ret = handle_http_txn_close(edata, *config);
    break;
  case TS_EVENT_VCONN_CLOSE:
    ret = handle_vconn_close(edata, *config);
    break;
  default:
    Dbg(dbg_ctl, "Unexpected event %d.", event);
    // We ignore the event, but we don't want to reject the connection.
    ret = TS_SUCCESS;
  }

  return ret;
}

void
TSPluginInit(int argc, char const **argv)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = PLUGIN_VENDOR;
  info.support_email = PLUGIN_SUPPORT_EMAIL;

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Failed to register.", PLUGIN_NAME);
    return;
  }

  if (argc != 2) {
    TSError("[%s] Usage: %s <config.yaml>", PLUGIN_NAME, argv[0]);
    return;
  }

  PluginConfigs configs;
  if (!load_config_file(argv[1], PluginType::GLOBAL, configs)) {
    TSError("[%s] Failed to load configuration from %s.", PLUGIN_NAME, argv[1]);
    return;
  }

  if (!prepare_configs(configs)) {
    return;
  }

  // Global configurations live for the life of the process: the log field callbacks and the
  // continuations below keep references to them, so release them from their unique_ptrs here.
  for (auto &owned_config : configs) {
    PluginConfig *config = owned_config.release();

    if (!config->log_symbol.empty()) {
      register_log_field(config);
    }

    TSCont cont = TSContCreate(main_handler, nullptr);
    TSContDataSet(cont, config);
    if (config->method.on_client_hello) {
      TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, cont);
    }
    if (config->standalone) {
      TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
    }
    if (config->method.type == Method::Type::CONNECTION_BASED) {
      TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, cont);
    } else {
      TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, cont);
    }
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  Dbg(dbg_ctl, "JAx Remap Plugin initializing..");
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  if (argc != 3) {
    snprintf(errbuf, errbuf_size, "[%s] Usage: @plugin=%s.so @pparam=<config.yaml>", PLUGIN_NAME, PLUGIN_NAME);
    return TS_ERROR;
  }
  Dbg(dbg_ctl, "New instance for client matching %s to %s", argv[0], argv[1]);

  auto configs = std::make_unique<PluginConfigs>();
  if (!load_config_file(argv[2], PluginType::REMAP, *configs)) {
    snprintf(errbuf, errbuf_size, "[%s] Failed to load configuration from %s", PLUGIN_NAME, argv[2]);
    return TS_ERROR;
  }

  if (!prepare_configs(*configs)) {
    return TS_ERROR;
  }

  for (auto const &config : *configs) {
    if (config->standalone) {
      Dbg(dbg_ctl, "Standalone mode. Adding hooks for %s.", config->method.name.data());
      config->handler = TSContCreate(main_handler, nullptr);
      if (config->method.on_client_hello) {
        TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, config->handler);
      }
      if (config->method.type == Method::Type::CONNECTION_BASED) {
        TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, config->handler);
      } else {
        TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, config->handler);
      }
      TSContDataSet(config->handler, config.get());
    }
  }

  // Past here the instance handle owns the configurations and TSRemapDeleteInstance releases them.
  *ih = static_cast<void *>(configs.release());

  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  auto configs = static_cast<PluginConfigs *>(ih);

  if (!configs || !rri) {
    TSError("[%s] Invalid private data or RRI or handler.", PLUGIN_NAME);
    return TSREMAP_NO_REMAP;
  }

  for (auto const &config : *configs) {
    handle_read_request_hdr(rh, *config);
  }

  return TSREMAP_NO_REMAP;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto configs = static_cast<PluginConfigs *>(ih);
  for (auto const &config : *configs) {
    if (config->handler) {
      // Destroying the continuation here causes a crash after remap.config reload
      // Instead of destroying, make it NOP.
      // TSContDestroy(config->handler);
      TSContDataSet(config->handler, nullptr);
    }
    if (config->log_handle) {
      flush_log_file(config->log_handle);
    }
  }
  delete configs;
}
