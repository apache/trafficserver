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

#include "ts/ts.h"
#include "ts/remap.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <string>
#include <string_view>

#define PLUGIN_NAME "cache_range_requests"
#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) TSError("[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

namespace
{
typedef enum parent_select_mode {
  PS_DEFAULT,      // Default ATS parent selection mode
  PS_CACHEKEY_URL, // Set parent selection url to cache_key url
} parent_select_mode_t;

struct pluginconfig {
  parent_select_mode_t ps_mode{PS_DEFAULT};
  bool consider_ims_header{false};
  bool modify_cache_key{true};
};

struct txndata {
  std::string range_value;
  time_t ims_time{0};
};

// Header for optional revalidation
constexpr std::string_view X_IMS_HEADER = {"X-Crr-Ims"};

// pluginconfig struct (global plugin only)
pluginconfig *gPluginConfig = {nullptr};

int handle_read_request_header(TSCont, TSEvent, void *);
void range_header_check(TSHttpTxn, pluginconfig *const);
void handle_send_origin_request(TSCont, TSHttpTxn, txndata *const);
void handle_client_send_response(TSHttpTxn, txndata *const);
void handle_server_read_response(TSHttpTxn, txndata *const);
int remove_header(TSMBuffer, TSMLoc, const char *, int);
bool set_header(TSMBuffer, TSMLoc, const char *, int, const char *, int);
int transaction_handler(TSCont, TSEvent, void *);
struct pluginconfig *create_pluginconfig(int argc, char *const argv[]);
void delete_pluginconfig(pluginconfig *const);

/**
 * Creates pluginconfig data structure
 * Sets default parent url selection mode
 * Walk plugin argument list and updates config
 */
pluginconfig *
create_pluginconfig(int argc, char *const argv[])
{
  DEBUG_LOG("Number of arguments: %d", argc);
  for (int index = 0; index < argc; ++index) {
    DEBUG_LOG("args[%d] = %s", index, argv[index]);
  }

  pluginconfig *const pc = new pluginconfig;

  if (nullptr == pc) {
    ERROR_LOG("Can't allocate pluginconfig");
    return nullptr;
  }

  static const struct option longopts[] = {
    {const_cast<char *>("ps-cachekey"), no_argument, nullptr, 'p'},
    {const_cast<char *>("consider-ims"), no_argument, nullptr, 'c'},
    {const_cast<char *>("no-modify-cachekey"), no_argument, nullptr, 'n'},
    {nullptr, 0, nullptr, 0},
  };

  // getopt assumes args start at '1'
  ++argc;
  --argv;

  for (;;) {
    int const opt = getopt_long(argc, argv, "", longopts, nullptr);
    if (-1 == opt) {
      break;
    }

    switch (opt) {
    case 'p': {
      DEBUG_LOG("Plugin modifies parent selection key");
      pc->ps_mode = PS_CACHEKEY_URL;
    } break;
    case 'c': {
      DEBUG_LOG("Plugin considers the '%.*s' header", (int)X_IMS_HEADER.size(), X_IMS_HEADER.data());
      pc->consider_ims_header = true;
    } break;
    case 'n': {
      DEBUG_LOG("Plugin doesn't modify cache key");
      pc->modify_cache_key = false;
    } break;
    default: {
    } break;
    }
  }

  // Backwards compatibility
  if (optind < argc && 0 == strcmp("ps_mode:cache_key_url", argv[optind])) {
    DEBUG_LOG("Plugin modifies parent selection key (deprecated)");
    pc->ps_mode = PS_CACHEKEY_URL;
  }

  return pc;
}

/**
 * Destroy pluginconfig data structure.
 */
void
delete_pluginconfig(pluginconfig *const pc)
{
  if (nullptr != pc) {
    DEBUG_LOG("Delete struct pluginconfig");
    delete pc;
  }
}

/**
 * Entry point when used as a global plugin.
 */
int
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
void
range_header_check(TSHttpTxn txnp, pluginconfig *const pc)
{
  char cache_key_url[8192] = {0};
  char *req_url;
  int length, url_length, cache_key_url_length;
  txndata *txn_state;
  TSMBuffer hdr_buf;
  TSMLoc hdr_loc = nullptr;
  TSMLoc loc     = nullptr;
  TSCont txn_contp;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_buf, &hdr_loc)) {
    loc = TSMimeHdrFieldFind(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
    if (TS_NULL_MLOC != loc) {
      const char *hdr_value = TSMimeHdrFieldValueStringGet(hdr_buf, hdr_loc, loc, 0, &length);
      TSHandleMLocRelease(hdr_buf, hdr_loc, loc);

      if (!hdr_value || length <= 0) {
        DEBUG_LOG("Not a range request.");
      } else {
        if (nullptr == (txn_contp = TSContCreate(static_cast<TSEventFunc>(transaction_handler), nullptr))) {
          ERROR_LOG("failed to create the transaction handler continuation.");
        } else {
          txn_state       = new txndata;
          std::string &rv = txn_state->range_value;
          rv.assign(hdr_value, length);
          DEBUG_LOG("length: %d, txn_state->range_value: %s", length, rv.c_str());
          req_url              = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
          cache_key_url_length = snprintf(cache_key_url, 8192, "%s-%s", req_url, rv.c_str());
          DEBUG_LOG("Rewriting cache URL for %s to %s", req_url, cache_key_url);
          if (req_url != nullptr) {
            TSfree(req_url);
          }

          if (nullptr != pc) {
            // set the cache key if configured to.
            if (pc->modify_cache_key && TS_SUCCESS != TSCacheUrlSet(txnp, cache_key_url, cache_key_url_length)) {
              ERROR_LOG("failed to change the cache url to %s.", cache_key_url);
              ERROR_LOG("Disabling cache for this transaction to avoid cache poisoning.");
              TSHttpTxnServerRespNoStoreSet(txnp, 1);
              TSHttpTxnRespCacheableSet(txnp, 0);
              TSHttpTxnReqCacheableSet(txnp, 0);
            }

            // Optionally set the parent_selection_url to the cache_key url or path
            if (PS_DEFAULT != pc->ps_mode) {
              TSMLoc ps_loc = nullptr;

              if (PS_CACHEKEY_URL == pc->ps_mode) {
                const char *start = cache_key_url;
                const char *end   = cache_key_url + cache_key_url_length;
                if (TS_SUCCESS == TSUrlCreate(hdr_buf, &ps_loc) &&
                    TS_PARSE_DONE == TSUrlParse(hdr_buf, ps_loc, &start, end) && // This should always succeed.
                    TS_SUCCESS == TSHttpTxnParentSelectionUrlSet(txnp, hdr_buf, ps_loc)) {
                  DEBUG_LOG("Set Parent Selection URL to cache_key_url: %s", cache_key_url);
                  TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, ps_loc);
                }
              }
            }

            // optionally consider an X-CRR-IMS header
            if (pc->consider_ims_header) {
              TSMLoc const imsloc = TSMimeHdrFieldFind(hdr_buf, hdr_loc, X_IMS_HEADER.data(), X_IMS_HEADER.size());
              if (TS_NULL_MLOC != imsloc) {
                time_t const itime = TSMimeHdrFieldValueDateGet(hdr_buf, hdr_loc, imsloc);
                DEBUG_LOG("Servicing the '%.*s' header", (int)X_IMS_HEADER.size(), X_IMS_HEADER.data());
                TSHandleMLocRelease(hdr_buf, hdr_loc, imsloc);
                if (0 < itime) {
                  txn_state->ims_time = itime;
                }
              }
            }
          }

          // remove the range request header.
          if (remove_header(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE) > 0) {
            DEBUG_LOG("Removed the Range: header from the request.");
          }

          TSContDataSet(txn_contp, txn_state);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
          DEBUG_LOG("Added TS_HTTP_SEND_REQUEST_HDR_HOOK, TS_HTTP_SEND_RESPONSE_HDR_HOOK, and TS_HTTP_TXN_CLOSE_HOOK");

          if (0 < txn_state->ims_time) {
            TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp);
            DEBUG_LOG("Also Added TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK");
          }
        }
      }
      // TSHandleMLocRelease(hdr_buf, hdr_loc, loc);
    } else {
      DEBUG_LOG("no range request header.");
    }
    TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, hdr_loc);
  } else {
    DEBUG_LOG("failed to retrieve the server request");
  }
}

/**
 * Restores the range request header if the request must be
 * satisfied from the origin and schedules the TS_READ_RESPONSE_HDR_HOOK.
 */
void
handle_send_origin_request(TSCont contp, TSHttpTxn txnp, txndata *const txn_state)
{
  TSMBuffer hdr_buf;
  TSMLoc hdr_loc = nullptr;

  std::string const &rv = txn_state->range_value;

  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &hdr_buf, &hdr_loc) && !rv.empty()) {
    if (set_header(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rv.data(), rv.length())) {
      DEBUG_LOG("Added range header: %s", rv.c_str());
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
  }
  TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, hdr_loc);
}

/**
 * Changes the response code back to a 206 Partial content before
 * replying to the client that requested a range.
 */
void
handle_client_send_response(TSHttpTxn txnp, txndata *const txn_state)
{
  bool partial_content_reason = false;
  char *p;
  int length;
  TSMBuffer resp_buf = nullptr;
  TSMBuffer req_buf  = nullptr;
  TSMLoc resp_loc    = nullptr;
  TSMLoc req_loc     = nullptr;

  TSReturnCode result = TSHttpTxnClientRespGet(txnp, &resp_buf, &resp_loc);
  DEBUG_LOG("result: %d", result);
  if (TS_SUCCESS == result) {
    TSHttpStatus status = TSHttpHdrStatusGet(resp_buf, resp_loc);
    // a cached result will have a TS_HTTP_OK with a 'Partial Content' reason
    if ((p = const_cast<char *>(TSHttpHdrReasonGet(resp_buf, resp_loc, &length))) != nullptr) {
      if ((length == 15) && (0 == strncasecmp(p, "Partial Content", length))) {
        partial_content_reason = true;
      }
    }
    DEBUG_LOG("%d %.*s", status, length, p);
    if (TS_HTTP_STATUS_OK == status && partial_content_reason) {
      DEBUG_LOG("Got TS_HTTP_STATUS_OK.");
      TSHttpHdrStatusSet(resp_buf, resp_loc, TS_HTTP_STATUS_PARTIAL_CONTENT);
      DEBUG_LOG("Set response header to TS_HTTP_STATUS_PARTIAL_CONTENT.");
    }
  }
  std::string const &rv = txn_state->range_value;
  // add the range request header back in so that range requests may be logged.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc) && !rv.empty()) {
    if (set_header(req_buf, req_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rv.data(), rv.length())) {
      DEBUG_LOG("added range header: %s", rv.c_str());
    } else {
      DEBUG_LOG("set_header() failed.");
    }
  } else {
    DEBUG_LOG("failed to get Request Headers");
  }
  TSHandleMLocRelease(resp_buf, TS_NULL_MLOC, resp_loc);
  TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
}

/**
 * After receiving a range request response from the origin, change
 * the response code from a 206 Partial content to a 200 OK so that
 * the response will be written to cache.
 */
void
handle_server_read_response(TSHttpTxn txnp, txndata *const txn_state)
{
  TSMBuffer resp_buf = nullptr;
  TSMLoc resp_loc    = nullptr;
  TSHttpStatus status;

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &resp_buf, &resp_loc)) {
    status = TSHttpHdrStatusGet(resp_buf, resp_loc);
    if (TS_HTTP_STATUS_PARTIAL_CONTENT == status) {
      DEBUG_LOG("Got TS_HTTP_STATUS_PARTIAL_CONTENT.");
      TSHttpHdrStatusSet(resp_buf, resp_loc, TS_HTTP_STATUS_OK);
      DEBUG_LOG("Set response header to TS_HTTP_STATUS_OK.");
      bool cacheable = TSHttpTxnIsCacheable(txnp, nullptr, resp_buf);
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
  TSHandleMLocRelease(resp_buf, TS_NULL_MLOC, resp_loc);
}

/**
 * Remove a header (fully) from an TSMLoc / TSMBuffer. Return the number
 * of fields (header values) we removed.
 *
 * From background_fetch.cc
 */
int
remove_header(TSMBuffer buf, TSMLoc hdr_loc, const char *header, int len)
{
  TSMLoc field = TSMimeHdrFieldFind(buf, hdr_loc, header, len);
  int cnt      = 0;

  while (field) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field);

    ++cnt;
    TSMimeHdrFieldDestroy(buf, hdr_loc, field);
    TSHandleMLocRelease(buf, hdr_loc, field);
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
bool
set_header(TSMBuffer buf, TSMLoc hdr_loc, const char *header, int len, const char *val, int val_len)
{
  if (!buf || !hdr_loc || !header || len <= 0 || !val || val_len <= 0) {
    return false;
  }

  DEBUG_LOG("header: %s, len: %d, val: %s, val_len: %d", header, len, val, val_len);
  bool ret         = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(buf, hdr_loc, header, len);

  if (!field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(buf, hdr_loc, header, len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(buf, hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
    }
  } else {
    TSMLoc tmp = nullptr;
    bool first = true;

    while (field_loc) {
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(buf, hdr_loc, field_loc);
      }
      tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field_loc);
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  return ret;
}

time_t
get_date_from_cached_hdr(TSHttpTxn txn)
{
  TSMBuffer buf;
  TSMLoc hdr_loc, date_loc;
  time_t date = 0;

  if (TSHttpTxnCachedRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
    date_loc = TSMimeHdrFieldFind(buf, hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (date_loc != TS_NULL_MLOC) {
      date = TSMimeHdrFieldValueDateGet(buf, hdr_loc, date_loc);
      TSHandleMLocRelease(buf, hdr_loc, date_loc);
    }
    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
  }

  return date;
}

/**
 * Handle a special IMS request.
 */
void
handle_cache_lookup_complete(TSHttpTxn txnp, txndata *const txn_state)
{
  int cachestat;
  if (TS_SUCCESS == TSHttpTxnCacheLookupStatusGet(txnp, &cachestat)) {
    if (TS_CACHE_LOOKUP_HIT_FRESH == cachestat) {
      time_t const ch_time = get_date_from_cached_hdr(txnp);
      DEBUG_LOG("IMS Cached header time %jd vs IMS %jd", static_cast<intmax_t>(ch_time),
                static_cast<intmax_t>(txn_state->ims_time));
      if (ch_time < txn_state->ims_time) {
        TSHttpTxnCacheLookupStatusSet(txnp, TS_CACHE_LOOKUP_HIT_STALE);
        if (TSIsDebugTagSet(PLUGIN_NAME)) {
          int url_len         = 0;
          char *const req_url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_len);
          if (nullptr != req_url) {
            std::string const &rv = txn_state->range_value;
            DEBUG_LOG("Forced revalidate %.*s-%s", url_len, req_url, rv.c_str());

            TSfree(req_url);
          }
        }
      }
    }
  }
}

/**
 * Transaction event handler.
 */
int
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp           = static_cast<TSHttpTxn>(edata);
  txndata *const txn_state = static_cast<txndata *>(TSContDataGet(contp));

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
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    handle_cache_lookup_complete(txnp, txn_state);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    if (txn_state != nullptr) {
      TSContDataSet(contp, nullptr);
      delete txn_state;
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
  char *const *plugin_argv = const_cast<char *const *>(argv);

  // Parse the argument list.
  *ih = static_cast<void *>(create_pluginconfig(argc - 2, plugin_argv + 2));

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
  pluginconfig *const pc = static_cast<pluginconfig *>(ih);

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
  pluginconfig *const pc = static_cast<pluginconfig *>(ih);

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
    if (1 < argc) {
      char *const *plugin_argv = const_cast<char *const *>(argv);
      gPluginConfig            = create_pluginconfig(argc - 1, plugin_argv + 1);
    }
  }

  if (nullptr == (txnp_cont = TSContCreate(static_cast<TSEventFunc>(handle_read_request_header), nullptr))) {
    ERROR_LOG("failed to create the transaction continuation handler.");
    return;
  } else {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txnp_cont);
  }
}
