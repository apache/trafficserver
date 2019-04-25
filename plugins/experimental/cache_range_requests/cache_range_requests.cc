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
 * requests are read across different disk drives reducing I/O
 * wait and load averages when there are large numbers of range
 * requests.
 */

#include <cstdio>
#include <cstring>
#include "ts/ts.h"
#include "ts/remap.h"

#define PLUGIN_NAME "cache_range_requests"
#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) TSError("[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

typedef enum parent_select_mode {
  PS_DEFAULT,      // Default ATS parent selection mode
  PS_CACHEKEY_URL, // Set parent selection url to cache_key url
} parent_select_mode_t;

struct pluginconfig {
  parent_select_mode_t ps_mode;
};

struct txndata {
  char *range_value;
};

static int handle_read_request_header(TSCont, TSEvent, void *);
static void range_header_check(TSHttpTxn txnp, struct pluginconfig *pc);
static void handle_send_origin_request(TSCont, TSHttpTxn, struct txndata *);
static void handle_client_send_response(TSHttpTxn, struct txndata *);
static void handle_server_read_response(TSHttpTxn, struct txndata *);
static int remove_header(TSMBuffer, TSMLoc, const char *, int);
static bool set_header(TSMBuffer, TSMLoc, const char *, int, const char *, int);
static int transaction_handler(TSCont, TSEvent, void *);
static struct pluginconfig *create_pluginconfig(int argc, const char *argv[]);
static void delete_pluginconfig(struct pluginconfig *);

// pluginconfig struct (global plugin only)
static struct pluginconfig *gPluginConfig = nullptr;

/**
 * Creates pluginconfig data structure
 * Sets default parent url selection mode
 * Walk plugin argument list and updates config
 */
static struct pluginconfig *
create_pluginconfig(int argc, const char *argv[])
{
  struct pluginconfig *pc = nullptr;

  pc = (struct pluginconfig *)TSmalloc(sizeof(struct pluginconfig));

  if (nullptr == pc) {
    ERROR_LOG("Can't allocate pluginconfig");
    return nullptr;
  }

  // Plugin uses default ATS selection (hash of URL path)
  pc->ps_mode = PS_DEFAULT;

  // Walk through param list.
  for (int c = 0; c < argc; c++) {
    if (strcmp("ps_mode:cache_key_url", argv[c]) == 0) {
      pc->ps_mode = PS_CACHEKEY_URL;
      break;
    }
  }

  return pc;
}

/**
 * Destroy pluginconfig data structure.
 */
static void
delete_pluginconfig(struct pluginconfig *pc)
{
  if (nullptr != pc) {
    DEBUG_LOG("Delete struct pluginconfig");
    TSfree(pc);
    pc = nullptr;
  }
}

/**
 * Entry point when used as a global plugin.
 *
 */
static int
handle_read_request_header(TSCont txn_contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  range_header_check(txnp, gPluginConfig);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
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
range_header_check(TSHttpTxn txnp, struct pluginconfig *pc)
{
  char cache_key_url[8192] = {0};
  char *req_url;
  int length, url_length, cache_key_url_length;
  struct txndata *txn_state;
  TSMBuffer hdr_bufp;
  TSMLoc req_hdrs = nullptr;
  TSMLoc loc      = nullptr;
  TSCont txn_contp;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_bufp, &req_hdrs)) {
    loc = TSMimeHdrFieldFind(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
    if (TS_NULL_MLOC != loc) {
      const char *hdr_value = TSMimeHdrFieldValueStringGet(hdr_bufp, req_hdrs, loc, 0, &length);
      if (!hdr_value || length <= 0) {
        DEBUG_LOG("Not a range request.");
      } else {
        if (nullptr == (txn_contp = TSContCreate((TSEventFunc)transaction_handler, nullptr))) {
          ERROR_LOG("failed to create the transaction handler continuation.");
        } else {
          txn_state              = (struct txndata *)TSmalloc(sizeof(struct txndata));
          txn_state->range_value = TSstrndup(hdr_value, length);
          DEBUG_LOG("length: %d, txn_state->range_value: %s", length, txn_state->range_value);
          txn_state->range_value[length] = '\0'; // workaround for bug in core

          req_url              = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
          cache_key_url_length = snprintf(cache_key_url, 8192, "%s-%s", req_url, txn_state->range_value);
          DEBUG_LOG("Rewriting cache URL for %s to %s", req_url, cache_key_url);
          if (req_url != nullptr) {
            TSfree(req_url);
          }

          // set the cache key.
          if (TS_SUCCESS != TSCacheUrlSet(txnp, cache_key_url, cache_key_url_length)) {
            DEBUG_LOG("failed to change the cache url to %s.", cache_key_url);
          }

          // Optionally set the parent_selection_url to the cache_key url or path
          if (nullptr != pc && PS_DEFAULT != pc->ps_mode) {
            TSMLoc ps_loc = nullptr;

            if (PS_CACHEKEY_URL == pc->ps_mode) {
              const char *start = cache_key_url;
              const char *end   = cache_key_url + cache_key_url_length;
              if (TS_SUCCESS == TSUrlCreate(hdr_bufp, &ps_loc) &&
                  TS_PARSE_DONE == TSUrlParse(hdr_bufp, ps_loc, &start, end) && // This should always succeed.
                  TS_SUCCESS == TSHttpTxnParentSelectionUrlSet(txnp, hdr_bufp, ps_loc)) {
                DEBUG_LOG("Set Parent Selection URL to cache_key_url: %s", cache_key_url);
                TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, ps_loc);
              }
            }
          }

          // remove the range request header.
          if (remove_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE) > 0) {
            DEBUG_LOG("Removed the Range: header from the request.");
          }

          TSContDataSet(txn_contp, txn_state);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
          DEBUG_LOG("Added TS_HTTP_SEND_REQUEST_HDR_HOOK, TS_HTTP_SEND_RESPONSE_HDR_HOOK, and TS_HTTP_TXN_CLOSE_HOOK");
        }
      }
      TSHandleMLocRelease(hdr_bufp, req_hdrs, loc);
    } else {
      DEBUG_LOG("no range request header.");
    }
    TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, req_hdrs);
  } else {
    DEBUG_LOG("failed to retrieve the server request");
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
  TSMLoc req_hdrs = nullptr;

  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &hdr_bufp, &req_hdrs) && txn_state->range_value != nullptr) {
    if (set_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, txn_state->range_value,
                   strlen(txn_state->range_value))) {
      DEBUG_LOG("Added range header: %s", txn_state->range_value);
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
  }
  TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, req_hdrs);
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
  TSMLoc resp_hdr, req_hdrs = nullptr;

  TSReturnCode result = TSHttpTxnClientRespGet(txnp, &response, &resp_hdr);
  DEBUG_LOG("result: %d", result);
  if (TS_SUCCESS == result) {
    TSHttpStatus status = TSHttpHdrStatusGet(response, resp_hdr);
    // a cached result will have a TS_HTTP_OK with a 'Partial Content' reason
    if ((p = (char *)TSHttpHdrReasonGet(response, resp_hdr, &length)) != nullptr) {
      if ((length == 15) && (0 == strncasecmp(p, "Partial Content", length))) {
        partial_content_reason = true;
      }
    }
    DEBUG_LOG("%d %.*s", status, length, p);
    if (TS_HTTP_STATUS_OK == status && partial_content_reason) {
      DEBUG_LOG("Got TS_HTTP_STATUS_OK.");
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_PARTIAL_CONTENT);
      DEBUG_LOG("Set response header to TS_HTTP_STATUS_PARTIAL_CONTENT.");
    }
  }
  // add the range request header back in so that range requests may be logged.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_bufp, &req_hdrs) && txn_state->range_value != nullptr) {
    if (set_header(hdr_bufp, req_hdrs, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, txn_state->range_value,
                   strlen(txn_state->range_value))) {
      DEBUG_LOG("added range header: %s", txn_state->range_value);
    } else {
      DEBUG_LOG("set_header() failed.");
    }
  } else {
    DEBUG_LOG("failed to get Request Headers");
  }
  TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
  TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, req_hdrs);
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

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &response, &resp_hdr)) {
    status = TSHttpHdrStatusGet(response, resp_hdr);
    if (TS_HTTP_STATUS_PARTIAL_CONTENT == status) {
      DEBUG_LOG("Got TS_HTTP_STATUS_PARTIAL_CONTENT.");
      TSHttpHdrStatusSet(response, resp_hdr, TS_HTTP_STATUS_OK);
      DEBUG_LOG("Set response header to TS_HTTP_STATUS_OK.");
      bool cacheable = TSHttpTxnIsCacheable(txnp, nullptr, response);
      DEBUG_LOG("range is cacheable: %d", cacheable);
    } else if (TS_HTTP_STATUS_OK == status) {
      DEBUG_LOG("The origin does not support range requests, attempting to disable cache write.");
      if (TS_SUCCESS == TSHttpTxnServerRespNoStoreSet(txnp, 1)) {
        DEBUG_LOG("Cache write has been disabled for this transaction.");
      } else {
        DEBUG_LOG("Unable to disable cache write for this transaction.");
      }
    }
  }
  TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
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
  int cnt      = 0;

  while (field) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field);

    ++cnt;
    TSMimeHdrFieldDestroy(bufp, hdr_loc, field);
    TSHandleMLocRelease(bufp, hdr_loc, field);
    field = tmp;
  }

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
  if (!bufp || !hdr_loc || !header || len <= 0 || !val || val_len <= 0) {
    return false;
  }

  DEBUG_LOG("header: %s, len: %d, val: %s, val_len: %d", header, len, val, val_len);
  bool ret         = false;
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
    TSMLoc tmp = nullptr;
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

  return ret;
}

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

  DEBUG_LOG("cache_range_requests remap is successfully initialized.");
  return TS_SUCCESS;
}

/**
 * New Remap instance
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /*errbuf */, int /* errbuf_size */)
{
  if (argc < 2) {
    ERROR_LOG("Remap argument list should contain at least 2 params"); // Should never happen..
    return TS_ERROR;
  }

  // Skip over the Remap params
  const char **plugin_argv = const_cast<const char **>(argv + 2);
  argc -= 2;

  // Parse the argument list.
  *ih = (struct pluginconfig *)create_pluginconfig(argc, plugin_argv);

  if (*ih == nullptr) {
    ERROR_LOG("Can't create pluginconfig");
  }

  return TS_SUCCESS;
}

/**
 * Delete Remap instance
 */
void
TSRemapDeleteInstance(void *ih)
{
  struct pluginconfig *pc = (struct pluginconfig *)ih;

  if (nullptr != pc) {
    delete_pluginconfig(pc);
  }
}

/**
 * Remap entry point.
 */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  struct pluginconfig *pc = (struct pluginconfig *)ih;

  range_header_check(txnp, pc);

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

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Comcast";
  info.support_email = (char *)"John_Rushford@cable.comcast.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    ERROR_LOG("Plugin registration failed.\n");
    ERROR_LOG("Unable to initialize plugin (disabled).");
    return;
  }

  if (nullptr == gPluginConfig) {
    if (argc > 1) {
      // Skip ahead of first param (name of traffic server plugin shared object)
      const char **plugin_argv = const_cast<const char **>(argv + 1);
      argc -= 1;
      gPluginConfig = create_pluginconfig(argc, plugin_argv);
    }
  }

  if (nullptr == (txnp_cont = TSContCreate((TSEventFunc)handle_read_request_header, nullptr))) {
    ERROR_LOG("failed to create the transaction continuation handler.");
    return;
  } else {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txnp_cont);
  }
}

/**
 * Transaction event handler.
 */
static int
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp            = static_cast<TSHttpTxn>(edata);
  struct txndata *txn_state = (struct txndata *)TSContDataGet(contp);

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
    if (txn_state != nullptr && txn_state->range_value != nullptr) {
      TSfree(txn_state->range_value);
    }
    if (txn_state != nullptr) {
      TSfree(txn_state);
    }
    TSContDestroy(contp);
    break;
  default:
    TSAssert(!"Unexpected event");
    break;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
