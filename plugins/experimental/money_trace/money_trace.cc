/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "tscore/BufferWriter.h"
#include "ts/ts.h"
#include "ts/remap.h"

#include <getopt.h>
#include <string_view>
#include <string>

#include "money_trace.h"

namespace
{
std::string_view const DefaultMimeHeader = "X-MoneyTrace";
enum PluginType { REMAP, GLOBAL };

struct Config {
  std::string header;             // override header
  std::string pregen_header;      // generate request header during remap
  std::string global_skip_header; // skip global plugin
  bool create_if_none = false;
  bool passthru       = false; // transparen mode: pass any headers through
};

Config *
config_from_args(int const argc, char const *argv[], PluginType const ptype)
{
  Config *const conf = new Config;

  static const struct option longopt[] = {
    {const_cast<char *>("passthru"), required_argument, nullptr, 'a'},
    {const_cast<char *>("create-if-none"), required_argument, nullptr, 'c'},
    {const_cast<char *>("global-skip-header"), required_argument, nullptr, 'g'},
    {const_cast<char *>("header"), required_argument, nullptr, 'h'},
    {const_cast<char *>("pregen-header"), required_argument, nullptr, 'p'},
    {nullptr, 0, nullptr, 0},
  };

  // getopt assumes args start at '1' so this hack is needed
  do {
    int const opt = getopt_long(argc, (char *const *)argv, "a:c:h:l:p:", longopt, nullptr);

    if (-1 == opt) {
      break;
    }

    LOG_DEBUG("Opt: %c", opt);

    switch (opt) {
    case 'a':
      if ("true" == std::string_view{optarg}) {
        LOG_DEBUG("Plugin acts as passthrough");
        conf->passthru = true;
      }
      break;
    case 'c':
      if ("true" == std::string_view{optarg}) {
        LOG_DEBUG("Plugin will create header if missing");
        conf->create_if_none = true;
      }
      break;
    case 'g':
      LOG_DEBUG("Using global-skip-header: '%s'", optarg);
      conf->global_skip_header.assign(optarg);
      break;
    case 'h':
      LOG_DEBUG("Using custom header: '%s'", optarg);
      conf->header.assign(optarg);
      break;
    case 'p':
      LOG_DEBUG("Using pregen_header '%s'", optarg);
      conf->pregen_header.assign(optarg);
      break;
    default:
      break;
    }
  } while (true);

  if (conf->header.empty()) {
    conf->header.assign(DefaultMimeHeader);
    LOG_DEBUG("Using default header name: '%.*s'", (int)DefaultMimeHeader.length(), DefaultMimeHeader.data());
  }

  if (conf->passthru && conf->create_if_none) {
    LOG_ERROR("passthru conflicts with create-if-none, disabling create-if-none!");
    conf->create_if_none = false;
  }

  if (REMAP == ptype && !conf->global_skip_header.empty()) {
    LOG_ERROR("--global-skip-header inappropriate for remap plugin, removing option!");
    conf->global_skip_header.clear();
  }

  return conf;
}

struct TxnData {
  std::string client_trace;
  std::string this_trace;
  Config const *config = nullptr;
};

std::string_view const traceid{"trace-id="};
std::string_view const parentid{"parent-id="};
std::string_view const spanid{"span-id="};
std::string_view const zerospan{"0"};
char const sep{';'};

std::string
next_trace(std::string_view const request_hdr, TSHttpTxn const txnp)
{
  std::string nexttrace;

  LOG_DEBUG("next_trace with '%.*s'", (int)request_hdr.length(), request_hdr.data());

  std::string_view view = request_hdr;

  // trace-id must be first
  if (0 != view.rfind(traceid, 0)) {
    LOG_DEBUG("Expected to find prefix '%.*s' in '%.*s'", (int)traceid.length(), traceid.data(), (int)view.length(), view.data());
    return nexttrace;
  }

  view = view.substr(traceid.length());

  // look for separator
  size_t seppos = view.find_first_of(sep);
  if (0 == seppos) {
    LOG_DEBUG("Trace is empty for '%.*s'", (int)request_hdr.length(), request_hdr.data());
    return nexttrace;
  }

  std::string_view trace = view.substr(0, seppos);

  if (std::string_view::npos == seppos) {
    LOG_DEBUG("Expected to find separator '%c' in %.*s", sep, (int)request_hdr.length(), request_hdr.data());
    view = std::string_view{};
  }

  std::string_view span;

  // scan remaining tokens
  while (!view.empty() && span.empty()) {
    // skip any leading whitespace
    while (!view.empty() && ' ' == view.front()) {
      view = view.substr(1);
    }

    // check for span-id field
    if (0 == view.rfind(spanid, 0)) {
      span   = view.substr(spanid.length());
      seppos = span.find_first_of(sep);
      span   = span.substr(0, seppos);

      // remove any trailing white space
      while (!span.empty() && ' ' == span.back()) {
        span = span.substr(0, span.length() - 1);
      }
    } else {
      LOG_DEBUG("Non '%.*s' found in '%.*s'", (int)spanid.length(), spanid.data(), (int)view.length(), view.data());
    }

    if (span.empty()) {
      seppos = view.find_first_of(sep);

      // move forward past sep
      if (std::string_view::npos != seppos) {
        view = view.substr(seppos + 1);
        LOG_DEBUG("Trimming view to '%.*s'", (int)view.length(), view.data());
      } else {
        view = std::string_view{};
      }
    }
  }

  if (span.empty()) {
    LOG_DEBUG("No span found, using default '%.*s'", (int)zerospan.length(), zerospan.data());
    span = zerospan;
  }

  // span becomes new parent
  ts::LocalBufferWriter<8192> bwriter;

  bwriter.write(traceid);
  bwriter.write(trace);
  bwriter.write(sep);
  bwriter.write(parentid);
  bwriter.write(span);
  bwriter.write(sep);
  bwriter.write(spanid);
  bwriter.print("{}", TSHttpTxnIdGet(txnp));

  nexttrace.assign(bwriter.view());

  return nexttrace;
}

std::string
create_trace(TSHttpTxn const txnp)
{
  std::string header;

  constexpr char new_parent{'0'};

  TSUuid const uuid = TSUuidCreate();
  if (nullptr != uuid) {
    if (TS_SUCCESS == TSUuidInitialize(uuid, TS_UUID_V4)) {
      char const *const uuidstr = TSUuidStringGet(uuid);
      if (nullptr != uuidstr) {
        ts::LocalBufferWriter<8192> bwriter;

        bwriter.write(traceid);
        bwriter.write(uuidstr);
        bwriter.write(sep);
        bwriter.write(parentid);
        bwriter.write(new_parent);
        bwriter.write(sep);
        bwriter.write(spanid);
        bwriter.print("{}", TSHttpTxnIdGet(txnp));

        header.assign(bwriter.view());
      } else {
        LOG_ERROR("Error getting uuid string");
      }
    } else {
      LOG_ERROR("Error initializing uuid");
    }

    TSUuidDestroy(uuid);
  } else {
    LOG_ERROR("Error calling TSUuidCreate");
  }

  return header;
}

/**
 * Creates header if necessary, sets given value.
 */
bool
set_header(TSMBuffer const bufp, TSMLoc const hdr_loc, std::string const &hdr, std::string const &val)
{
  bool isset = false;

  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, hdr.data(), hdr.length());

  if (TS_NULL_MLOC == field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, hdr.data(), hdr.length(), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val.data(), val.length())) {
        TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        LOG_DEBUG("header/value added: '%s' '%s'", hdr.c_str(), val.c_str());
        isset = true;
      } else {
        LOG_DEBUG("unable to set: '%s' to '%s'", hdr.c_str(), val.c_str());
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    } else {
      LOG_DEBUG("unable to create: '%s'", hdr.c_str());
    }
  } else {
    bool first = true;
    while (field_loc) {
      TSMLoc const tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val.data(), val.length())) {
          LOG_DEBUG("header/value set: '%s' '%s'", hdr.c_str(), val.c_str());
          isset = true;
        } else {
          LOG_DEBUG("unable to set: '%s' to '%s'", hdr.c_str(), val.c_str());
        }
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  return isset;
}

/**
 * The TS_EVENT_HTTP_POST_REMAP callback.
 *
 * If global_skip_header is set the global plugin
 * will check for it here.
 */
void
global_skip_check(TSCont const contp, TSHttpTxn const txnp, TxnData *const txn_data)
{
  Config const *const conf = txn_data->config;
  if (conf->global_skip_header.empty()) {
    LOG_ERROR("Called in error, no global skip header defined!");
    return;
  }

  // Check for a money trace header.  Route accordingly.
  TSMBuffer bufp = nullptr;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSMLoc const field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, conf->global_skip_header.c_str(), conf->global_skip_header.length());
    if (TS_NULL_MLOC != field_loc) {
      LOG_DEBUG("global_skip_header found, disabling for the rest of this transaction");
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    } else { // schedule continuations
      if (conf->create_if_none || !txn_data->client_trace.empty()) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
      }
      if (!txn_data->client_trace.empty()) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      }
    }
  }
}

/**
 * The TS_EVENT_HTTP_SEND_REQUEST_HDR callback.
 *
 * When a parent request is made, this function adds the new
 * money trace header to the parent request headers.
 */
void
send_server_request(TSHttpTxn const txnp, TxnData *const txn_data)
{
  Config const *const conf = txn_data->config;
  if (txn_data->this_trace.empty()) {
    if (conf->passthru) {
      txn_data->this_trace = txn_data->client_trace;
    } else if (!txn_data->client_trace.empty()) {
      txn_data->this_trace = next_trace(txn_data->client_trace, txnp);
    } else if (conf->create_if_none) {
      txn_data->this_trace = create_trace(txnp);
    }
  }

  if (txn_data->this_trace.empty()) {
    if (conf->create_if_none) {
      LOG_DEBUG("Unable to deal with client trace '%s', creating new", txn_data->client_trace.c_str());
      txn_data->this_trace = create_trace(txnp);
    } else {
      LOG_DEBUG("Unable to deal with client trace '%s', passing through!", txn_data->client_trace.c_str());
      txn_data->this_trace = txn_data->client_trace;
    }
  }

  TSMBuffer bufp = nullptr;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc)) {
    if (!set_header(bufp, hdr_loc, txn_data->config->header, txn_data->this_trace)) {
      LOG_ERROR("Unable to set the server request trace header '%s'", txn_data->this_trace.c_str());
    }
  } else {
    LOG_ERROR("Unable to get the txn server request");
  }
}

/**
 * The TS_EVENT_HTTP_SEND_RESPONSE_HDR callback.
 *
 * Adds the money trace header received in the client request to the
 * client response headers.
 */
void
send_client_response(TSHttpTxn const txnp, TxnData *const txn_data)
{
  LOG_DEBUG("send_client_response");

  if (txn_data->client_trace.empty()) {
    LOG_DEBUG("no client trace data to return.");
    return;
  }

  // send back the original money trace header received in the
  // client request back in the response to the client.
  TSMBuffer bufp = nullptr;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    if (!set_header(bufp, hdr_loc, txn_data->config->header, txn_data->client_trace)) {
      LOG_ERROR("Unable to set the client response trace header.");
    }
  } else {
    LOG_DEBUG("Unable to get the txn client response");
  }
}

/**
 * Transaction event handler.
 */
int
transaction_handler(TSCont const contp, TSEvent const event, void *const edata)
{
  TSHttpTxn const txnp    = static_cast<TSHttpTxn>(edata);
  TxnData *const txn_data = static_cast<TxnData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_POST_REMAP:
    LOG_DEBUG("global plugin checking for skip header");
    global_skip_check(contp, txnp, txn_data);
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    LOG_DEBUG("updating send request headers.");
    send_server_request(txnp, txn_data);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    LOG_DEBUG("updating send response headers.");
    send_client_response(txnp, txn_data);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    LOG_DEBUG("handling transaction close.");
    delete txn_data;
    TSContDestroy(contp);
    break;
  default:
    TSAssert(!"Unexpected event");
    break;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return TS_SUCCESS;
}

/**
 * Check for the existence of a money trace header.
 * If there is one, schedule the continuation to call back and
 * process on send request or send response.
 * Global plugin may need to schedule a hook to check for skip header.
 */
void
check_request_header(TSHttpTxn const txnp, Config const *const conf, PluginType const ptype)
{
  TxnData *txn_data = nullptr;

  // Check for a money trace header.  Route accordingly.
  TSMBuffer bufp = nullptr;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSMLoc const field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, conf->header.c_str(), conf->header.length());
    if (TS_NULL_MLOC != field_loc) {
      int length            = 0;
      const char *hdr_value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &length);
      if (!hdr_value || length <= 0) {
        LOG_DEBUG("ignoring, corrupt trace header.");
      } else {
        txn_data         = new TxnData;
        txn_data->config = conf;
        txn_data->client_trace.assign(hdr_value, length);
        LOG_DEBUG("found monetrace header: '%.*s', length: %d", length, hdr_value, length);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    } else if (!conf->passthru && conf->create_if_none) {
      txn_data             = new TxnData;
      txn_data->config     = conf;
      txn_data->this_trace = create_trace(txnp);
      LOG_DEBUG("created trace header: '%s'", txn_data->this_trace.c_str());
    } else {
      LOG_DEBUG("no trace header handling for this request.");
    }

    // Check for pregen_header
    if (nullptr != txn_data && !conf->pregen_header.empty()) {
      if (txn_data->this_trace.empty()) {
        txn_data->this_trace = next_trace(txn_data->client_trace, txnp);

        if (txn_data->this_trace.empty()) {
          if (conf->create_if_none) {
            LOG_DEBUG("Unable to deal with client trace '%s', creating new", txn_data->client_trace.c_str());
            txn_data->this_trace = create_trace(txnp);
          } else {
            LOG_DEBUG("Unable to deal with client trace '%s', passing through!", txn_data->client_trace.c_str());
            txn_data->this_trace = txn_data->client_trace;
          }
        }
      }
      if (!txn_data->this_trace.empty()) {
        if (!set_header(bufp, hdr_loc, conf->pregen_header, txn_data->this_trace)) {
          LOG_ERROR("Unable to set the client request pregen trace header.");
        }
      }
    }
  } else {
    LOG_DEBUG("unable to get the request request");
  }

  // Schedule appropriate continuations
  if (nullptr != txn_data) {
    TSCont const contp = TSContCreate(transaction_handler, nullptr);
    if (nullptr != contp) {
      TSContDataSet(contp, txn_data);
      TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);

      // global plugin may need to check for skip header
      if (GLOBAL == ptype && !conf->global_skip_header.empty()) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_POST_REMAP_HOOK, contp);
      } else {
        if (conf->create_if_none || !txn_data->client_trace.empty()) {
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
        }
        if (!txn_data->client_trace.empty()) {
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
        }
      }
    } else {
      LOG_ERROR("failed to create the transaction handler continuation");
      delete txn_data;
    }
  }
}

int
global_request_header_hook(TSCont const contp, TSEvent const event, void *const edata)
{
  TSHttpTxn const txnp     = static_cast<TSHttpTxn>(edata);
  Config const *const conf = static_cast<Config *>(TSContDataGet(contp));
  check_request_header(txnp, conf, GLOBAL);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

/**
 * Remap initialization.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  LOG_DEBUG("money_trace remap is successfully initialized.");

  return TS_SUCCESS;
}

/**
 * not used, one instance per remap and no parameters are used.
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /*errbuf */, int /* errbuf_size */)
{
  // second arg poses as the program name
  --argc;
  ++argv;
  Config *const conf = config_from_args(argc, const_cast<char const **>(argv), REMAP);
  *ih                = static_cast<void *>(conf);
  return TS_SUCCESS;
}

/**
 * not used, one instance per remap
 */
void
TSRemapDeleteInstance(void *ih)
{
  Config *const conf = static_cast<Config *>(ih);
  delete conf;
}

/**
 * Remap entry point.
 */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  Config const *const conf = static_cast<Config *>(ih);
  check_request_header(txnp, conf, REMAP);
  return TSREMAP_NO_REMAP;
}

/**
 * global plugin initialization
 */
void
TSPluginInit(int argc, char const *argv[])
{
  LOG_DEBUG("Starting global plugin init");

  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    LOG_ERROR("Plugin registration failed");
    return;
  }

  Config const *const conf = config_from_args(argc, argv, GLOBAL);

  TSCont const contp = TSContCreate(global_request_header_hook, nullptr);
  TSContDataSet(contp, (void *)conf);

  // This fires before any remap hooks.
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
