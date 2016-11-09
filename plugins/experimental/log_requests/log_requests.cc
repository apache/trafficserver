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

// we log the set of errors in {errors} - {blacklist}
static std::vector<TSHttpStatus> blacklist;
static std::vector<TSHttpStatus> errors({TS_HTTP_STATUS_BAD_REQUEST,
                                         TS_HTTP_STATUS_UNAUTHORIZED,
                                         TS_HTTP_STATUS_PAYMENT_REQUIRED,
                                         TS_HTTP_STATUS_FORBIDDEN,
                                         TS_HTTP_STATUS_NOT_FOUND,
                                         TS_HTTP_STATUS_METHOD_NOT_ALLOWED,
                                         TS_HTTP_STATUS_NOT_ACCEPTABLE,
                                         TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED,
                                         TS_HTTP_STATUS_REQUEST_TIMEOUT,
                                         TS_HTTP_STATUS_CONFLICT,
                                         TS_HTTP_STATUS_GONE,
                                         TS_HTTP_STATUS_LENGTH_REQUIRED,
                                         TS_HTTP_STATUS_PRECONDITION_FAILED,
                                         TS_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE,
                                         TS_HTTP_STATUS_REQUEST_URI_TOO_LONG,
                                         TS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,
                                         TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE,
                                         TS_HTTP_STATUS_EXPECTATION_FAILED,
                                         TS_HTTP_STATUS_UNPROCESSABLE_ENTITY,
                                         TS_HTTP_STATUS_LOCKED,
                                         TS_HTTP_STATUS_FAILED_DEPENDENCY,
                                         TS_HTTP_STATUS_UPGRADE_REQUIRED,
                                         TS_HTTP_STATUS_PRECONDITION_REQUIRED,
                                         TS_HTTP_STATUS_TOO_MANY_REQUESTS,
                                         TS_HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
                                         TS_HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                         TS_HTTP_STATUS_NOT_IMPLEMENTED,
                                         TS_HTTP_STATUS_BAD_GATEWAY,
                                         TS_HTTP_STATUS_SERVICE_UNAVAILABLE,
                                         TS_HTTP_STATUS_GATEWAY_TIMEOUT,
                                         TS_HTTP_STATUS_HTTPVER_NOT_SUPPORTED,
                                         TS_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES,
                                         TS_HTTP_STATUS_INSUFFICIENT_STORAGE,
                                         TS_HTTP_STATUS_LOOP_DETECTED,
                                         TS_HTTP_STATUS_NOT_EXTENDED,
                                         TS_HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED});

static bool should_log(TSHttpTxn txnp);
static const std::string convert_http_version(const int version);
static void log_request_line(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_response_status_line(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_headers(TSMBuffer bufp, TSMLoc loc, std::string output_header);
static void log_full_transaction(TSHttpTxn txnp);
static int log_requests_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata);

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

  // else, if resp_status is in errors, log it
  if (std::any_of(errors.begin(), errors.end(), [=](TSHttpStatus error) { return error == resp_status; }))
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
  int url_len;
  const char *method;
  const char *url;
  TSMLoc url_loc;

  // parse method
  method = TSHttpHdrMethodGet(bufp, loc, &method_len);

  // parse version
  const std::string version = convert_http_version(TSHttpHdrVersionGet(bufp, loc));

  // parse request line URL
  TSHttpHdrUrlGet(bufp, loc, &url_loc);
  url = TSUrlStringGet(bufp, url_loc, &url_len);

  // get rid of the preceeding http:// on the request URI
  for (int i = 0; i < 7; ++i) {
    url++;
  }
  url_len -= 7;

  TSError(B_PLUGIN_NAME " [%s] request line is:\n%.*s %.*s %s\n", output_header.c_str(), method_len, method, url_len, url,
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
  TSMBuffer txn_resp_bufp;
  TSMLoc txn_resp_loc;

  TSError(B_PLUGIN_NAME " --- begin transaction ---");

  // get client request/response
  if (TSHttpTxnClientReqGet(txnp, &txn_req_bufp, &txn_req_loc) != TS_SUCCESS ||
      TSHttpTxnClientRespGet(txnp, &txn_resp_bufp, &txn_resp_loc) != TS_SUCCESS) {
    TSError(B_PLUGIN_NAME " Couldn't retrieve transaction information. Aborting this transaction log");
    return;
  }

  // log the request/response
  log_request_line(txn_req_bufp, txn_req_loc, "Client request");
  log_headers(txn_req_bufp, txn_req_loc, "Client request");
  log_response_status_line(txn_resp_bufp, txn_resp_loc, "Client response");
  log_headers(txn_resp_bufp, txn_resp_loc, "Client response");

  // release memory handles
  TSHandleMLocRelease(txn_req_bufp, TS_NULL_MLOC, txn_req_loc);
  TSHandleMLocRelease(txn_resp_bufp, TS_NULL_MLOC, txn_resp_loc);

  TSError(B_PLUGIN_NAME " --- end transaction ---");
}

static int
log_requests_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE:
    if (should_log(txnp))
      log_full_transaction(txnp);
    return 0;

  default:
    TSError("[log-requests] Unexpected event received.");
    break;
  }
  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Evil Inc.";
  info.support_email = "invalidemail@invalid.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(B_PLUGIN_NAME " Plugin registration failed.");
  }

  // populate blacklist
  if (argc >= 2 && strcmp("--no-log", argv[1]) == 0) {
    for (int i = 2; i < argc; ++i) {
      blacklist.push_back(static_cast<TSHttpStatus>(atoi(argv[i])));
    }
  }

  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(log_requests_plugin, NULL));
}
