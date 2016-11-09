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

#include "ts/ts.h"
#include "ts/ink_defs.h"

#define PLUGIN_NAME log_requests

// we log the set of errors in {errors} - {blacklist}
static TSHttpStatus *blacklist;
static int blacklist_size; // number of entries in blacklist
static TSHttpStatus errors[] = {TS_HTTP_STATUS_BAD_REQUEST,
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
                                TS_HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED};

static bool should_log(TSHttpTxn txnp);
static void log_request_line(TSMBuffer bufp, TSMLoc loc, const char *output_header);
static void log_response_status_line(TSMBuffer bufp, TSMLoc loc, const char *output_header);
static void log_headers(TSMBuffer bufp, TSMLoc loc, const char *output_header);
static void log_full_transaction(TSHttpTxn txnp);
static int log_requests_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata);

static bool
should_log(TSHttpTxn txnp)
{
  TSMBuffer txn_resp_bufp;
  TSMLoc txn_resp_loc;
  TSHttpStatus resp_status;

  if (TSHttpTxnClientRespGet(txnp, &txn_resp_bufp, &txn_resp_loc) != TS_SUCCESS) {
    TSError("[log_requests] Couldn't retrieve server response header.");
    return false;
  }

  // if resp_status is blacklisted, don't log
  resp_status = TSHttpHdrStatusGet(txn_resp_bufp, txn_resp_loc);
  for (int i = 0; i < blacklist_size; ++i) {
    if (resp_status == blacklist[i])
      return false;
  }

  // else, if resp_status is in errors, log it
  for (int i = 0; i < (int)(sizeof(errors) / sizeof(TSHttpStatus)); ++i) {
    if (resp_status == errors[i]) {
      return true;
    }
  }

  // fall through
  return false;
}

const char *
convert_http_version(const int version)
{
  char *ret;
  if (version == TS_HTTP_VERSION(1, 0))
    ret = "HTTP/1.0";
  else if (version == TS_HTTP_VERSION(1, 1))
    ret = "HTTP/1.1";
  else if (version == TS_HTTP_VERSION(1, 2))
    ret = "HTTP/1.2";
  else if (version == TS_HTTP_VERSION(2, 0))
    ret = "HTTP/2.0";
  else {
    ret = "(Unknown HTTP version)";
  }

  return ret;
}

static void
log_request_line(TSMBuffer bufp, TSMLoc loc, const char *output_header)
{
  int method_len;
  int url_len;
  const char *version;
  TSMLoc url_loc;

  // parse method
  const char *method = TSHttpHdrMethodGet(bufp, loc, &method_len);

  // parse version
  version = convert_http_version(TSHttpHdrVersionGet(bufp, loc));

  // parse request line URL
  TSHttpHdrUrlGet(bufp, loc, &url_loc);
  const char *url = TSUrlStringGet(bufp, url_loc, &url_len);

  // null terminate these strings so we can printf them
  char *dup_method   = TSstrndup(method, method_len);
  char *dup_url      = TSstrndup(url, url_len);
  char *dup_url_orig = dup_url;

  // get rid of the preceeding http:// on the request URI
  for (int i = 0; i < 7; ++i) {
    dup_url++;
  }

  TSError("[log_requests] %s request line='%s %s %s'", output_header, dup_method, dup_url, version);

  TSfree(dup_method);
  TSfree(dup_url_orig);
}

static void
log_response_status_line(TSMBuffer bufp, TSMLoc loc, const char *output_header)
{
  const char *version;
  TSHttpStatus status_code;
  const char *explanation;
  int explanation_len;

  // parse version
  version = convert_http_version(TSHttpHdrVersionGet(bufp, loc));

  // parse status code
  status_code = TSHttpHdrStatusGet(bufp, loc);

  // get explanation
  explanation           = TSHttpHdrReasonGet(bufp, loc, &explanation_len);
  char *explanation_dup = TSstrndup(explanation, explanation_len);

  TSError("[log_requests] %s response status line='%s %d %s'", output_header, version, status_code, explanation_dup);

  TSfree(explanation_dup);
}

static void
log_headers(TSMBuffer bufp, TSMLoc loc, const char *output_header)
{
  // parse out request headers
  TSMLoc field_loc = TSMimeHdrFieldGet(bufp, loc, 0);
  while (field_loc) {
    TSMLoc next_field_loc;
    const char *name;
    int name_len;

    // grab the header name
    name = TSMimeHdrFieldNameGet(bufp, loc, field_loc, &name_len);

    if (name) {
      char *dup_name = TSstrndup(name, name_len);
      TSError("[log_requests] %s Header=%s", output_header, dup_name);
      TSfree(dup_name);

      // get the header value(s)
      int n_values;
      n_values = TSMimeHdrFieldValuesCount(bufp, loc, field_loc);
      if (n_values && (n_values != TS_ERROR)) {
        const char *value = NULL;
        int value_len     = 0;

        for (int i = 0; i < n_values; ++i) {
          value = TSMimeHdrFieldValueStringGet(bufp, loc, field_loc, i, &value_len);
          if (value != NULL || value_len) {
            char *dup_value = TSstrndup(value, value_len);
            TSError("[log_requests] --->Value=%s", dup_value);
            TSfree(dup_value);
          }
        }
      }
    }

    // get the memory location to the next header field
    next_field_loc = TSMimeHdrFieldNext(bufp, loc, field_loc);
    TSHandleMLocRelease(bufp, loc, field_loc);
    field_loc = next_field_loc;
  }
}

static void
log_full_transaction(TSHttpTxn txnp)
{
  TSMBuffer txn_req_bufp;
  TSMLoc txn_req_loc;
  TSMBuffer txn_resp_bufp;
  TSMLoc txn_resp_loc;

  TSError("[log_requests] --- begin transaction ---");

  // get client request/response
  if (TSHttpTxnClientReqGet(txnp, &txn_req_bufp, &txn_req_loc) != TS_SUCCESS ||
      TSHttpTxnClientRespGet(txnp, &txn_resp_bufp, &txn_resp_loc) != TS_SUCCESS) {
    TSError("[log_requests] Couldn't retrieve transaction information. Aborting this transaction log");
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

  TSError("[log_requests] --- end transaction ---");
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

  info.plugin_name   = "log-requests";
  info.vendor_name   = "Evil Inc.";
  info.support_email = "invalidemail@invalid.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[log-requests] Plugin registration failed.");
  }

  // populate blacklist
  blacklist_size = 0;
  blacklist      = (TSHttpStatus *)malloc(1024 * sizeof(TSHttpStatus));
  if (argc >= 2 && strcmp("--no-log", argv[1]) == 0) {
    for (int i = 2; i < argc; ++i) {
      blacklist[blacklist_size] = atoi(argv[i]);
      ++blacklist_size;
    }
  }

  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(log_requests_plugin, NULL));
}
