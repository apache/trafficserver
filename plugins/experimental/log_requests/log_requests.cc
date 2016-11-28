/** @file

  Logs full request/response headers on an error response code (4xx/5xx)

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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <vector>
#include <algorithm>
#include <string>

#include "ts/ts.h"
#include "ts/ink_defs.h"

#define PLUGIN_NAME "log_requests"
#define B_PLUGIN_NAME "[" PLUGIN_NAME "]"

// we log the set of errors in {4xx/5xx errors} - {blacklist}
static std::vector<TSHttpStatus> blacklist;

// some plugin argument stuff
static char arg_blacklist[2048] = "";
static bool log_proxy           = false;

// forward declarations
static bool should_log(TSHttpTxn txnp);
static const std::string convert_http_version(const int version);
static void log_request_line(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_response_status_line(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_headers(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_full_transaction(TSHttpTxn txnp);
static int log_requests_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata);
static std::vector<std::string> split(const std::string &str, const std::string &delim);

static std::vector<std::string>
split(const std::string &str, const std::string &delim)
{
  std::vector<std::string> tokens;
  size_t prev = 0, pos = 0;
  do {
    pos = str.find(delim, prev);
    if (pos == std::string::npos)
      pos = str.length();

    std::string token = str.substr(prev, pos - prev);
    if (!token.empty())
      tokens.push_back(token);

    prev = pos + delim.length();
  } while (pos < str.length() && prev < str.length());

  return tokens;
}

static bool
should_log(TSHttpTxn txnp)
{
  TSMBuffer txn_resp_bufp;
  TSMLoc txn_resp_loc;
  TSHttpStatus resp_status;

  if (TSHttpTxnClientRespGet(txnp, &txn_resp_bufp, &txn_resp_loc) != TS_SUCCESS) {
    TSError(B_PLUGIN_NAME " Couldn't retrieve server response header.");
    return false;
  }

  // get the transaction response status code
  resp_status = TSHttpHdrStatusGet(txn_resp_bufp, txn_resp_loc);

  // if resp_status is blacklisted, don't log
  if (std::any_of(blacklist.begin(), blacklist.end(), [=](TSHttpStatus code) { return code == resp_status; }))
    return false;

  // else, if resp_status is a 4xx/5xx error, log it
  if (static_cast<int>(resp_status) >= 400 && static_cast<int>(resp_status) < 600)
    return true;

  // fall through
  return false;
}

static const std::string
convert_http_version(const int version)
{
  if (version == TS_HTTP_VERSION(1, 0))
    return "HTTP/1.0";
  else if (version == TS_HTTP_VERSION(1, 1))
    return "HTTP/1.1";
  else if (version == TS_HTTP_VERSION(1, 2))
    return "HTTP/1.2";
  else if (version == TS_HTTP_VERSION(2, 0))
    return "HTTP/2.0";
  else {
    return "(Unknown HTTP version)";
  }
}

static void
log_request_line(TSMBuffer bufp, TSMLoc loc, std::string output_header)
{
  int method_len;
  int path_len;
  const char *method;
  const char *path;
  TSMLoc url_loc;

  // parse method
  method = TSHttpHdrMethodGet(bufp, loc, &method_len);

  // parse version
  const std::string version = convert_http_version(TSHttpHdrVersionGet(bufp, loc));

  // parse request line URL
  TSHttpHdrUrlGet(bufp, loc, &url_loc);
  path = TSUrlPathGet(bufp, url_loc, &path_len);

  TSError(B_PLUGIN_NAME " [%s] request line is:\n%.*s /%.*s %s\n", output_header.c_str(), method_len, method, path_len, path,
          version.c_str());
}

static void
log_response_status_line(TSMBuffer bufp, TSMLoc loc, std::string output_header)
{
  TSHttpStatus status_code;
  const char *explanation;
  int explanation_len;

  // parse version
  const std::string version = convert_http_version(TSHttpHdrVersionGet(bufp, loc));

  // parse status code
  status_code = TSHttpHdrStatusGet(bufp, loc);

  // get explanation
  explanation = TSHttpHdrReasonGet(bufp, loc, &explanation_len);

  TSError(B_PLUGIN_NAME " [%s] response status line is:\n%s %d %.*s\n", output_header.c_str(), version.c_str(), status_code,
          explanation_len, explanation);
}

static void
log_headers(TSMBuffer bufp, TSMLoc loc, std::string output_header)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail;

  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  // This will print just MIMEFields and not the http request line
  TSMimeHdrPrint(bufp, loc, output_buffer);

  // We need to loop over all the buffer blocks, there can be more than 1
  block = TSIOBufferReaderStart(reader);
  do {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    if (block_avail > 0) {
      TSError(B_PLUGIN_NAME " Headers are:\n%.*s", static_cast<int>(block_avail), block_start);
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  // Free up the TSIOBuffer that we used to print out the header
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);
}

static void
log_full_transaction(TSHttpTxn txnp)
{
  TSMBuffer txn_req_bufp;
  TSMLoc txn_req_loc;
  TSMBuffer txn_proxy_req_bufp;
  TSMLoc txn_proxy_req_loc;
  TSMBuffer txn_proxy_resp_bufp;
  TSMLoc txn_proxy_resp_loc;
  TSMBuffer txn_resp_bufp;
  TSMLoc txn_resp_loc;

  TSError(B_PLUGIN_NAME " --- begin transaction ---");

  // get client request/response
  bool clientreq_ok  = TSHttpTxnClientReqGet(txnp, &txn_req_bufp, &txn_req_loc) == TS_SUCCESS;
  bool clientresp_ok = TSHttpTxnClientRespGet(txnp, &txn_resp_bufp, &txn_resp_loc) == TS_SUCCESS;

  // log client request/response
  if (clientreq_ok && clientresp_ok) {
    log_request_line(txn_req_bufp, txn_req_loc, "Client request");
    log_headers(txn_req_bufp, txn_req_loc, "Client request");
    log_response_status_line(txn_resp_bufp, txn_resp_loc, "Client response");
    log_headers(txn_resp_bufp, txn_resp_loc, "Client response");
  } else {
    TSError(B_PLUGIN_NAME " Couldn't retrieve client transaction information. Aborting this transaction log");
  }

  // log the proxy request and proxy reponse if flag enabled
  if (log_proxy) {
    // get proxy request/response
    bool proxyreq_ok  = TSHttpTxnServerReqGet(txnp, &txn_proxy_req_bufp, &txn_proxy_req_loc) == TS_SUCCESS;
    bool proxyresp_ok = TSHttpTxnServerRespGet(txnp, &txn_proxy_resp_bufp, &txn_proxy_resp_loc) == TS_SUCCESS;

    // log proxy request/response
    if (proxyreq_ok && proxyresp_ok) {
      log_request_line(txn_proxy_req_bufp, txn_proxy_req_loc, "Proxy request");
      log_headers(txn_proxy_req_bufp, txn_proxy_req_loc, "Proxy request");
      log_response_status_line(txn_proxy_resp_bufp, txn_proxy_resp_loc, "Proxy response");
      log_headers(txn_proxy_resp_bufp, txn_proxy_resp_loc, "Proxy response");
    } else {
      TSError(B_PLUGIN_NAME " Couldn't retrieve proxy transaction information. Aborting this transaction log");
    }

    // release memory handles
    if (proxyreq_ok)
      TSHandleMLocRelease(txn_proxy_req_bufp, TS_NULL_MLOC, txn_proxy_req_loc);
    if (proxyresp_ok)
      TSHandleMLocRelease(txn_proxy_resp_bufp, TS_NULL_MLOC, txn_proxy_resp_loc);
  }

  TSError(B_PLUGIN_NAME " --- end transaction ---");

  // release memory handles
  if (clientreq_ok)
    TSHandleMLocRelease(txn_req_bufp, TS_NULL_MLOC, txn_req_loc);
  if (clientresp_ok)
    TSHandleMLocRelease(txn_resp_bufp, TS_NULL_MLOC, txn_resp_loc);
}

static int
log_requests_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE:
    if (should_log(txnp))
      log_full_transaction(txnp);
    return 0;

  default:
    TSError(B_PLUGIN_NAME " Unexpected event received.");
    break;
  }
  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char **argv ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  // do fun plugin registration stuff
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Evil Inc.";
  info.support_email = "invalidemail@invalid.com";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(B_PLUGIN_NAME " Plugin registration failed.");
  }

  // parse plugin args (defined in plugin.config)
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--log-proxy") == 0) {
      log_proxy = true;
    } else if (strcmp(argv[i], "--no-log") == 0) {
      if (argv[++i]) {
        strncpy(arg_blacklist, argv[i], sizeof(arg_blacklist) - 1);
      }
    }
  }

  TSError(B_PLUGIN_NAME " Plugin arg: log-proxy=%s", log_proxy ? "on" : "off");
  TSError(B_PLUGIN_NAME " Plugin arg: no-log=%s", arg_blacklist);

  // populate blacklist
  if (*arg_blacklist) {
    auto tokens = split(std::string(arg_blacklist), std::string(","));
    for (auto &tok : tokens) {
      blacklist.push_back(static_cast<TSHttpStatus>(atoi(tok.c_str())));
    }
  }

  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(log_requests_plugin, NULL));
}
