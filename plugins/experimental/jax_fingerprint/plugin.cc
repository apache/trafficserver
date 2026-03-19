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

#include "ja4/ja4_method.h"
#include "ja4h/ja4h_method.h"
#include "ja3/ja3_method.h"

#include <ts/apidefs.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/remap_version.h>

#include <getopt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <version>

DbgCtl dbg_ctl{PLUGIN_NAME};

namespace
{

} // end anonymous namespace

static bool
read_config_option(int argc, char const *argv[], PluginConfig &config)
{
  const struct option longopts[] = {
    {"standalone",   no_argument,       nullptr, 's'},
    {"method",       required_argument, nullptr, 'M'}, // JA4, JA4H, or JA3
    {"mode",         required_argument, nullptr, 'm'}, // overwrite, keep, or append
    {"header",       required_argument, nullptr, 'h'},
    {"via-header",   required_argument, nullptr, 'v'},
    {"log-filename", required_argument, nullptr, 'f'},
    {"servernames",  required_argument, nullptr, 'S'},
    {nullptr,        0,                 nullptr, 0  }
  };

  optind = 0;
  int opt{0};
  while ((opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopts, nullptr)) >= 0) {
    switch (opt) {
    case '?':
      Dbg(dbg_ctl, "Unrecognized command argument.");
      break;
    case 'M':
      if (strcmp("JA4", optarg) == 0) {
        config.method = ja4_method::method;
      } else if (strcmp("JA4H", optarg) == 0) {
        config.method = ja4h_method::method;
      } else if (strcmp("JA3", optarg) == 0) {
        config.method = ja3_method::method;
      } else {
        Dbg(dbg_ctl, "Unexpected method: %s", optarg);
        return false;
      }
      break;
    case 'm':
      if (strcmp("overwrite", optarg) == 0) {
        config.mode = Mode::OVERWRITE;
      } else if (strcmp("keep", optarg) == 0) {
        config.mode = Mode::KEEP;
      } else if (strcmp("append", optarg) == 0) {
        config.mode = Mode::APPEND;
      } else {
        Dbg(dbg_ctl, "Unexpected mode: %s", optarg);
        return false;
      }
      break;
    case 'h':
      config.header_name = {optarg, strlen(optarg)};
      break;
    case 'v':
      config.via_header_name = {optarg, strlen(optarg)};
      break;
    case 'f':
      config.log_filename = {optarg, strlen(optarg)};
      break;
    case 's':
      config.standalone = true;
      break;
    case 'S':
      for (std::string_view input(optarg, strlen(optarg)); !input.empty();) {
        auto pos = input.find(',');
        config.servernames.emplace(input.substr(0, pos));
        input.remove_prefix(pos == std::string_view::npos ? input.size() : pos + 1);
      }
      break;
    case 0:
    case -1:
      break;
    default:
      Dbg(dbg_ctl, "Unexpected options error.");
      return false;
    }
  }

  Dbg(dbg_ctl, "JAx method is %s", config.method.name);
  Dbg(dbg_ctl, "JAx mode is %d", static_cast<int>(config.mode));
  Dbg(dbg_ctl, "JAx header is %s", !config.header_name.empty() ? config.header_name.c_str() : "DISABLED");
  Dbg(dbg_ctl, "JAx via-header is %s", !config.via_header_name.empty() ? config.via_header_name.c_str() : "DISABLED");
  Dbg(dbg_ctl, "JAx log file is %s", !config.log_filename.empty() ? config.log_filename.c_str() : "DISABLED");
  for (auto &&servername : config.servernames) {
    Dbg(dbg_ctl, "%s", servername.c_str());
  }

  return true;
}

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
    ctx = new JAxContext(config.method.name, TSNetVConnRemoteAddrGet(vconn));
    set_user_arg(vconn, config, ctx);
  }

  if (config.method.on_client_hello) {
    config.method.on_client_hello(ctx, vconn);
  }

  TSVConnReenable(vconn);

  return TS_SUCCESS;
}

int
handle_read_request_hdr(void *edata, PluginConfig &config)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  if (txnp == nullptr) {
    Dbg(dbg_ctl, "Failed to get txn object.");
    return TS_SUCCESS;
  }

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
    ctx = new JAxContext(config.method.name, TSNetVConnRemoteAddrGet(vconn));
    set_user_arg(container, config, ctx);
  }

  if (config.method.on_request) {
    config.method.on_request(ctx, txnp);
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

  delete get_user_arg(txnp, config);
  set_user_arg(txnp, config, nullptr);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_vconn_close(void *edata, PluginConfig &config)
{
  TSVConn vconn = static_cast<TSVConn>(edata);

  delete get_user_arg(vconn, config);
  set_user_arg(vconn, config, nullptr);

  TSVConnReenable(vconn);
  return TS_SUCCESS;
}

int
main_handler(TSCont cont, TSEvent event, void *edata)
{
  int ret;

  auto config = static_cast<PluginConfig *>(TSContDataGet(cont));

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

  PluginConfig *config = new PluginConfig();
  config->plugin_type  = PluginType::GLOBAL;

  if (!read_config_option(argc, argv, *config)) {
    TSError("[%s] Failed to parse options.", PLUGIN_NAME);
    return;
  }

  if (!config->log_filename.empty()) {
    if (!create_log_file(config->log_filename, config->log_handle)) {
      TSError("[%s] Failed to create log.", PLUGIN_NAME);
      return;
    } else {
      Dbg(dbg_ctl, "Created log file.");
    }
  }

  if (reserve_user_arg(*config) == TS_ERROR) {
    TSError("[%s] Failed to reserve user arg index.", PLUGIN_NAME);
    return;
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

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  Dbg(dbg_ctl, "JAx Remap Plugin initializing..");
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  Dbg(dbg_ctl, "New instance for client matching %s to %s", argv[0], argv[1]);
  auto config         = new PluginConfig();
  config->plugin_type = PluginType::REMAP;

  // Parse parameters
  if (!read_config_option(argc - 1, const_cast<const char **>(argv + 1), *config)) {
    delete config;
    Dbg(dbg_ctl, "Bad arguments");
    return TS_ERROR;
  }

  // Create a log file
  if (!config->log_filename.empty()) {
    if (!create_log_file(config->log_filename, config->log_handle)) {
      TSError("[%s] Failed to create log.", PLUGIN_NAME);
      return TS_ERROR;
    } else {
      Dbg(dbg_ctl, "Created log file.");
    }
  }

  if (reserve_user_arg(*config) == TS_ERROR) {
    TSError("[%s] Failed to reserve user arg index.", PLUGIN_NAME);
    return TS_ERROR;
  }

  // Create continuation
  if (config->standalone) {
    config->handler = TSContCreate(main_handler, nullptr);
    if (config->method.on_client_hello) {
      TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, config->handler);
    }
    if (config->method.type == Method::Type::CONNECTION_BASED) {
      TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, config->handler);
    } else {
      TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, config->handler);
    }
    TSContDataSet(config->handler, config);
  }

  *ih = static_cast<void *>(config);

  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  auto config = static_cast<PluginConfig *>(ih);

  if (!config || !rri) {
    TSError("[%s] Invalid private data or RRI or handler.", PLUGIN_NAME);
    return TSREMAP_NO_REMAP;
  }

  handle_read_request_hdr(rh, *config);

  return TSREMAP_NO_REMAP;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto config = static_cast<PluginConfig *>(ih);
  if (config->handler) {
    TSContDestroy(config->handler);
  }
  if (config->log_handle) {
    flush_log_file(config->log_handle);
  }
  delete config;
}
