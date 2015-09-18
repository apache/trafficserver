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

/*
 * This plugin looks for range requests and then creates a new
 * cache key url so that each individual range requests is written
 * to the cache as a individual object so that subsequent range
 * requests are read accross different disk drives reducing I/O
 * wait and load averages when there are large numbers of range
 * requests.
 */

#include <stdio.h>
#include <string.h>
#include "ts/ts.h"
#include "ts/remap.h"

#define PLUGIN_NAME "cache_range_requests"

struct txndata {
  char *range_value;
};

static void handle_read_request_header(TSCont, TSEvent, void *);
static void range_header_check(TSHttpTxn txnp);
static void handle_send_origin_request(TSCont, TSHttpTxn, struct txndata *);
static void handle_client_send_response(TSHttpTxn, struct txndata *);
static void handle_server_read_response(TSHttpTxn, struct txndata *);
static int remove_header(TSMBuffer, TSMLoc, const char *, int);
static bool set_header(TSMBuffer, TSMLoc, const char *, int, const char *, int);
static void transaction_handler(TSCont, TSEvent, void *);

/**
 * Entry point when used as a global plugin.
 *
 */
static void
handle_read_request_header(TSCont txn_contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  TSDebug(PLUGIN_NAME, "Starting handle_read_request_header()");

  range_header_check(txnp);

  TSDebug(PLUGIN_NAME, "End of handle_read_request_header()");
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

/**
 * Reads the client request header and if this is a range request:
 *
 * 1. creates a new cache key url using the range request information.
 * 2. Saves the range request information and then removes the range
 *    header so that the response retrieved from the origin will
 *    be written to cache.
 * 3. Schedules TS_HTTP_SEND_REQUEST_HDR_HOOK, TS_HTTP_SEND_RESPONSE_HDR_HOOK,
 *    and TS_HTTP_TXN_CLOSE_HOOK for further processing.
 */
static void
range_header_check(TSHttpTxn txnp)
{
  char cache_key_url[8192] = {0};
  char *req_url;
  int length, url_length;
  struct txndata *txn_state;
  TSMBuffer hdr_bufp;
  TSMLoc req_hdrs = NULL;
  TSMLoc loc = NULL;
  TSCont txn_contp;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_bufp, &req_hdrs)) {
    loc = TSMimeHdrFieldFind(hdr_bufp, req_hdrs, "Range", -1);
    if (TS_NULL_MLOC != loc) {
      const char *hdr_value = TSMimeHdrFieldValueStringGet(hdr_bufp, req_hdrs, loc, 0, &length);
      if (!hdr_value || length <= 0) {
        TSDebug(PLUGIN_NAME, "range_header_check(): Not a range request.");
      } else {
        if (NULL == (txn_contp = TSContCreate((TSEventFunc)transaction_handler, NULL))) {
          TSError("[%s] range_header_check(): failed to create the transaction handler continuation.", PLUGIN_NAME);
        } else {
          txn_state = (struct txndata *)TSmalloc(sizeof(struct txndata));
          txn_state->range_value = TSstrndup(hdr_value, length);
          TSDebug(PLUGIN_NAME, "range_header_check(): length = %d, txn_state->range_value = %s", length, txn_state->range_value);
          txn_state->range_value[length] = '\0'; // workaround for bug in core

          req_url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
          snprintf(cache_key_url, 8192, "%s-%s", req_url, txn_state->range_value);
          TSDebug(PLUGIN_NAME, "range_header_check(): Rewriting cache URL for %s to %s", req_url, cache_key_url);
          if (req_url != NULL)
            TSfree(req_url);

          // set the cache key.
          if (TS_SUCCESS != TSCacheUrlSet(txnp, cache_key_url, strlen(cache_key_url))) {
            TSDebug(PLUGIN_NAME, "range_header_check(): failed to change the cache url to %s.", cache_key_url);
          }
          // remove the range request header.
          if (remove_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE) > 0) {
            TSDebug(PLUGIN_NAME, "range_header_check(): Removed the Range: header from request");
          }

          TSContDataSet(txn_contp, txn_state);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
          TSDebug(PLUGIN_NAME, "range_header_check(): Added TS_HTTP_SEND_REQUEST_HDR_HOOK, TS_HTTP_SEND_RESPONSE_HDR_HOOK, and TS_HTTP_TXN_CLOSE_HOOK");
        }
      }
      TSHandleMLocRelease(hdr_bufp, req_hdrs, loc);
    } else {
      TSDebug(PLUGIN_NAME, "range_header_check(): no range request header.");
    }
    TSHandleMLocRelease(hdr_bufp, req_hdrs, NULL);
  } else {
    TSDebug(PLUGIN_NAME, "range_header_check(): failed to retrieve the server request.");
  }
}

/**
 * Restores the range request header if the request must be
 * satisfied from the origin and schedules the TS_READ_RESPONSE_HDR_HOOK.
 */
static void
handle_send_origin_request(TSCont contp, TSHttpTxn txnp, struct txndata *txn_state)
{
  TSMBuffer hdr_bufp;
  TSMLoc req_hdrs = NULL;

  TSDebug(PLUGIN_NAME, "Starting handle_send_origin_request()");
  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &hdr_bufp, &req_hdrs) && txn_state->range_value != NULL) {
    if (set_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, txn_state->range_value,
                   strlen(txn_state->range_value))) {
      TSDebug(PLUGIN_NAME, "handle_send_origin_request(): Added range header: %s", txn_state->range_value);
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
  }
  TSHandleMLocRelease(hdr_bufp, req_hdrs, NULL);
  TSDebug(PLUGIN_NAME, "End of handle_send_origin_request()");
}

/**
 * Changes the response code back to a 206 Partial content before
 * replying to the client that requested a range.
 */
static void
handle_client_send_response(TSHttpTxn txnp, struct txndata *txn_state)
{
  bool partial_content_reason = false;
  char *p;
  int length;
  TSMBuffer response, hdr_bufp;
  TSMLoc resp_hdr, req_hdrs = NULL;

  TSDebug(PLUGIN_NAME, "Starting handle_client_send_response ()");

  TSReturnCode result = TSHttpTxnClientRespGet(txnp, &response, &resp_hdr);
  TSDebug(PLUGIN_NAME, "handle_client_send_response(): result %d", result);
  if (TS_SUCCESS == result) {
    TSHttpStatus status = TSHttpHdrStatusGet(response, resp_hdr);
    // a cached result will have a TS_HTTP_OK with a 'Partial Content' reason
    if ((p = (char *)TSHttpHdrReasonGet(response, resp_hdr, &length)) != NULL) {
      if ((length == 15) && (0 == strncasecmp(p, "Partial Content", length))) {
        partial_content_reason = true;
      }
    }
    TSDebug(PLUGIN_NAME, "client_send_response() %d %.*s", status, length, p);
    if (TS_HTTP_STATUS_OK == status && partial_content_reason) {
      TSDebug(PLUGIN_NAME, "handle_client_send_response(): Got TS_HTTP_STATUS_OK.");
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_PARTIAL_CONTENT);
      TSDebug(PLUGIN_NAME, "handle_client_send_response(): Set response header to TS_HTTP_STATUS_PARTIAL_CONTENT.");
    }
  }
  // add the range request header back in so that range requests may be logged.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_bufp, &req_hdrs) && txn_state->range_value != NULL) {
    if (set_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, txn_state->range_value,
                   strlen(txn_state->range_value))) {
      TSDebug(PLUGIN_NAME, "handle_client_send_response(): added range header: %s", txn_state->range_value);
    }
    else {
      TSDebug(PLUGIN_NAME, "handle_client_send_response(): set_header() failed.");
    }
  }
  else {
    TSDebug(PLUGIN_NAME, "handle_client_send_response(): failed to get Request Headers."); 
  }
  TSHandleMLocRelease(response, resp_hdr, NULL);
  TSHandleMLocRelease(hdr_bufp, req_hdrs, NULL);
  TSDebug(PLUGIN_NAME, "End of handle_client_send_response()");
}

/**
 * After receiving a range request response from the origin, change
 * the response code from a 206 Partial content to a 200 OK so that
 * the response will be written to cache.
 */
static void
handle_server_read_response(TSHttpTxn txnp, struct txndata *txn_state)
{
  TSMBuffer response;
  TSMLoc resp_hdr;
  TSHttpStatus status;

  TSDebug(PLUGIN_NAME, "Starting handle_server_read_response()");

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &response, &resp_hdr)) {
    status = TSHttpHdrStatusGet(response, resp_hdr);
    if (TS_HTTP_STATUS_PARTIAL_CONTENT == status) {
      TSDebug(PLUGIN_NAME, "handle_server_read_response(): Got TS_HTTP_STATUS_PARTIAL_CONTENT.");
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_OK);
      TSDebug(PLUGIN_NAME, "handle_server_read_response(): Set response header to TS_HTTP_STATUS_OK.");
      bool cacheable = TSHttpTxnIsCacheable(txnp, NULL, response);
      TSDebug(PLUGIN_NAME, "handle_server_read_response(): range is cacheable: %d", cacheable);
    } else if (TS_HTTP_STATUS_OK == status) {
      TSDebug(PLUGIN_NAME, "handle_server_read_response(): The origin does not support range requests, attempting to disable cache write.");
      if (TS_SUCCESS == TSHttpTxnServerRespNoStoreSet(txnp, 1)) {
        TSDebug(PLUGIN_NAME, "handle_server_read_response(): Cache write has been disabled for this transaction.");
      } else {
        TSDebug(PLUGIN_NAME, "handle_server_read_response(): Unable to disable cache write for this transaction.");
      }
    }
  }
  TSHandleMLocRelease(response, resp_hdr, NULL);
  TSDebug(PLUGIN_NAME, "handle_server_read_response(): End of handle_server_read_response ()");
}

/**
 * Remove a header (fully) from an TSMLoc / TSMBuffer. Return the number
 * of fields (header values) we removed.
 *
 * From background_fetch.cc
 */
static int
remove_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, int len)
{
  TSMLoc field = TSMimeHdrFieldFind(bufp, hdr_loc, header, len);
  int cnt = 0;

  TSDebug(PLUGIN_NAME, "Starting remove_header()");

  while (field) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field);

    ++cnt;
    TSMimeHdrFieldDestroy(bufp, hdr_loc, field);
    TSHandleMLocRelease(bufp, hdr_loc, field);
    field = tmp;
  }

  TSDebug(PLUGIN_NAME, "End of remove_header()");
  return cnt;
}

/**
 * Set a header to a specific value. This will avoid going to through a
 * remove / add sequence in case of an existing header.
 * but clean.
 *
 * From background_fetch.cc
 */
static bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, int len, const char *val, int val_len)
{
  TSDebug(PLUGIN_NAME, "Starting set_header()");

  if (!bufp || !hdr_loc || !header || len <= 0 || !val || val_len <= 0) {
    return false;
  }

  TSDebug(PLUGIN_NAME, "set_header(): header = %s, len = %d, val = %s, val_len = %d\n", header, len, val, val_len);
  bool ret = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header, len);

  if (!field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header, len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
  } else {
    TSMLoc tmp = NULL;
    bool first = true;

    while (field_loc) {
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  TSDebug(PLUGIN_NAME, "End of set_header()");
  return ret;
}

/**
 * Remap initialization.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "cache_range_requests remap init");
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "cache_range_requests remap is successfully initialized");
  return TS_SUCCESS;
}

/**
 * not used.
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /*errbuf */, int /* errbuf_size */)
{
  TSDebug(PLUGIN_NAME, "TSRemapNewInstance()");

  return TS_SUCCESS;
}

/**
 * not used.
 */
void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(PLUGIN_NAME, "TSRemapDeleteInstance()");
}

/**
 * Remap entry point.
 */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap()");

  range_header_check(txnp);
  return TSREMAP_NO_REMAP;
}

/**
 * Global plugin initialization.
 */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont txnp_cont;

  TSDebug(PLUGIN_NAME, "Starting TSPluginInit()");

  info.plugin_name = (char *)PLUGIN_NAME;
  info.vendor_name = (char *)"Comcast";
  info.support_email = (char *)"John_Rushford@cable.comcast.com";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("[%s] TSPluginInit(): Plugin registration failed.\n", PLUGIN_NAME);
    TSError("[%s] Unable to initialize plugin (disabled).\n", PLUGIN_NAME);
    return;
  }

  if (NULL == (txnp_cont = TSContCreate((TSEventFunc)handle_read_request_header, NULL))) {
    TSError("[%s] TSContCreate(): failed to create the transaction continuation handler.", PLUGIN_NAME);
    return;
  } else {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txnp_cont);
  }
}

/**
 * Transaction event handler.
 */
static void
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  struct txndata *txn_state = (struct txndata *)TSContDataGet(contp);

  TSDebug(PLUGIN_NAME, "Starting transaction_handler()");
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_server_read_response(txnp, txn_state);
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    handle_send_origin_request(contp, txnp, txn_state);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_client_send_response(txnp, txn_state);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(PLUGIN_NAME, "Starting handle_transaction_close().");
    if (txn_state != NULL && txn_state->range_value != NULL)
      TSfree(txn_state->range_value);
    if (txn_state != NULL)
      TSfree(txn_state);
    TSContDestroy(contp);
    break;
  default:
    TSAssert(!"Unexpected event");
    break;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  TSDebug(PLUGIN_NAME, "End of transaction_handler()");
}
