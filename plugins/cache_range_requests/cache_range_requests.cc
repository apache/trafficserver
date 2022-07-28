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
using parent_select_mode_t = enum parent_select_mode {
  PS_DEFAULT,      // Default ATS parent selection mode
  PS_CACHEKEY_URL, // Set parent selection url to cache_key url
};

constexpr std::string_view DefaultImsHeader = {"X-Crr-Ims"};
constexpr std::string_view SLICE_CRR_HEADER = {"Slice-Crr-Status"};
constexpr std::string_view SLICE_CRR_VAL    = "1";

struct pluginconfig {
  parent_select_mode_t ps_mode{PS_DEFAULT};
  bool consider_ims_header{false};
  bool modify_cache_key{true};
  bool verify_cacheability{false};
  bool cache_complete_responses{false};
  std::string ims_header;
};

struct txndata {
  std::string range_value;
  TSHttpStatus origin_status{TS_HTTP_STATUS_NONE};
  time_t ims_time{0};
  bool verify_cacheability{false};
  bool cache_complete_responses{false};
  bool slice_response{false};
  bool slice_request{false};
};

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
    {const_cast<char *>("consider-ims"), no_argument, nullptr, 'c'},
    {const_cast<char *>("ims-header"), required_argument, nullptr, 'i'},
    {const_cast<char *>("no-modify-cachekey"), no_argument, nullptr, 'n'},
    {const_cast<char *>("ps-cachekey"), no_argument, nullptr, 'p'},
    {const_cast<char *>("verify-cacheability"), no_argument, nullptr, 'v'},
    {const_cast<char *>("cache-complete-responses"), no_argument, nullptr, 'r'},
    {nullptr, 0, nullptr, 0},
  };

  // getopt assumes args start at '1'
  ++argc;
  --argv;

  for (;;) {
    int const opt = getopt_long(argc, argv, "i:", longopts, nullptr);
    if (-1 == opt) {
      break;
    }

    switch (opt) {
    case 'c': {
      DEBUG_LOG("Plugin considers the ims header");
      pc->consider_ims_header = true;
    } break;
    case 'i': {
      DEBUG_LOG("Plugin uses custom ims header: %s", optarg);
      pc->ims_header.assign(optarg);
      pc->consider_ims_header = true;
    } break;
    case 'n': {
      DEBUG_LOG("Plugin doesn't modify cache key");
      pc->modify_cache_key = false;
    } break;
    case 'p': {
      DEBUG_LOG("Plugin modifies parent selection key");
      pc->ps_mode = PS_CACHEKEY_URL;
    } break;
    case 'v': {
      DEBUG_LOG("Plugin verifies whether the object in the transaction is cacheable");
      pc->verify_cacheability = true;
    } break;
    case 'r': {
      DEBUG_LOG("Plugin allows complete responses (200 OK) to be cached");
      pc->cache_complete_responses = true;
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

  if (pc->consider_ims_header && pc->ims_header.empty()) {
    pc->ims_header = DefaultImsHeader;
    DEBUG_LOG("Plugin uses default ims header: %s", pc->ims_header.c_str());
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
  TSMBuffer hdr_buf = nullptr;
  TSMLoc hdr_loc    = TS_NULL_MLOC;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_buf, &hdr_loc)) {
    TSMLoc const range_loc = TSMimeHdrFieldFind(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
    if (TS_NULL_MLOC != range_loc) {
      int len                     = 0;
      char const *const hdr_value = TSMimeHdrFieldValueStringGet(hdr_buf, hdr_loc, range_loc, 0, &len);

      if (!hdr_value || len <= 0) {
        DEBUG_LOG("Not a range request.");
      } else {
        txndata *const txn_state = new txndata;
        txn_state->range_value.assign(hdr_value, len);

        std::string const &rv = txn_state->range_value;
        DEBUG_LOG("txn_state->range_value: '%s'", rv.c_str());

        // Consider config options
        if (nullptr != pc) {
          char cache_key_url[16384] = {0};
          int cache_key_url_len     = 0;

          if (pc->modify_cache_key || PS_CACHEKEY_URL == pc->ps_mode) {
            int url_len         = 0;
            char *const req_url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_len);
            cache_key_url_len   = snprintf(cache_key_url, sizeof(cache_key_url), "%s-%s", req_url, rv.c_str());
            DEBUG_LOG("Forming new cache URL for '%s': '%.*s'", req_url, cache_key_url_len, cache_key_url);
            if (req_url != nullptr) {
              TSfree(req_url);
            }
          }

          // Modify the cache_key
          if (pc->modify_cache_key) {
            DEBUG_LOG("Setting cache key to '%.*s'", cache_key_url_len, cache_key_url);
            if (TS_SUCCESS != TSCacheUrlSet(txnp, cache_key_url, cache_key_url_len)) {
              ERROR_LOG("Failed to change the cache url, disabling cache for this transaction to avoid cache poisoning.");
              TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SERVER_NO_STORE, true);
              TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_RESPONSE_CACHEABLE, false);
              TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_REQUEST_CACHEABLE, false);
            }
          }

          // Set the parent_selection_url to the modified cache_key.
          if (PS_CACHEKEY_URL == pc->ps_mode) {
            TSMLoc ps_loc     = TS_NULL_MLOC;
            const char *start = cache_key_url;
            const char *end   = cache_key_url + cache_key_url_len;
            if (TS_SUCCESS == TSUrlCreate(hdr_buf, &ps_loc)) {
              if (TS_PARSE_DONE == TSUrlParse(hdr_buf, ps_loc, &start, end) && // This should always succeed.
                  TS_SUCCESS == TSHttpTxnParentSelectionUrlSet(txnp, hdr_buf, ps_loc)) {
                DEBUG_LOG("Setting Parent Selection URL to '%.*s'", cache_key_url_len, cache_key_url);
              }
              TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, ps_loc);
            }
          }

          // optionally consider an ims header
          if (pc->consider_ims_header) {
            TSMLoc const imsloc = TSMimeHdrFieldFind(hdr_buf, hdr_loc, pc->ims_header.data(), pc->ims_header.size());
            if (TS_NULL_MLOC != imsloc) {
              time_t const itime = TSMimeHdrFieldValueDateGet(hdr_buf, hdr_loc, imsloc);
              DEBUG_LOG("Servicing the '%s' header", pc->ims_header.c_str());
              TSHandleMLocRelease(hdr_buf, hdr_loc, imsloc);
              if (0 < itime) {
                txn_state->ims_time = itime;
              }
            }
          }

          txn_state->verify_cacheability      = pc->verify_cacheability;
          txn_state->cache_complete_responses = pc->cache_complete_responses;
        }

        // remove the range request header.
        if (remove_header(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE) > 0) {
          DEBUG_LOG("Removed the Range: header from the request.");
        }

        // Set up the continuation
        TSCont const txn_contp = TSContCreate(transaction_handler, nullptr);
        TSContDataSet(txn_contp, txn_state);
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp);
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp);
        TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
        DEBUG_LOG("Added TS_HTTP_SEND_REQUEST_HDR_HOOK, TS_HTTP_SEND_RESPONSE_HDR_HOOK, and TS_HTTP_TXN_CLOSE_HOOK");

        if (0 < txn_state->ims_time) {
          TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp);
          DEBUG_LOG("Also Added TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK");
        }

        // check if slice requests for cache lookup status
        TSMLoc const locfield(TSMimeHdrFieldFind(hdr_buf, hdr_loc, SLICE_CRR_HEADER.data(), SLICE_CRR_HEADER.size()));
        if (nullptr != locfield) {
          TSHandleMLocRelease(hdr_buf, hdr_loc, locfield);
          txn_state->slice_request = true;
        }
      }
      TSHandleMLocRelease(hdr_buf, hdr_loc, range_loc);
    } else {
      DEBUG_LOG("No range request header.");
    }
    TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, hdr_loc);
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
  TSMLoc hdr_loc = TS_NULL_MLOC;

  std::string const &rv = txn_state->range_value;
  if (rv.empty()) {
    ERROR_LOG("txn_state->range_value unexpectedly empty!");
    return;
  }

  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &hdr_buf, &hdr_loc) && !rv.empty()) {
    if (set_header(hdr_buf, hdr_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rv.data(), rv.size())) {
      DEBUG_LOG("Added range header: %s", rv.c_str());
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
  }
  TSHandleMLocRelease(hdr_buf, TS_NULL_MLOC, hdr_loc);
}

/**
 * Changes the response status back to a 206 before
 * replying to the client that requested a range.
 */
void
handle_client_send_response(TSHttpTxn txnp, txndata *const txn_state)
{
  bool partial_content_reason = false;

  // Detect header modified by this plugin (200 response)
  TSMBuffer resp_buf = nullptr;
  TSMLoc resp_loc    = TS_NULL_MLOC;
  if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &resp_buf, &resp_loc)) {
    TSHttpStatus const status = TSHttpHdrStatusGet(resp_buf, resp_loc);
    // a cached status will be 200 with expected parent response status of 206
    if (TS_HTTP_STATUS_OK == status) {
      if (txn_state->origin_status == TS_HTTP_STATUS_NONE ||
          txn_state->origin_status == TS_HTTP_STATUS_NOT_MODIFIED) { // cache hit or revalidation
        // status is always TS_HTTP_STATUS_NONE on cache hit; its value is only set during handle_server_read_response()
        TSMLoc content_range_loc = TSMimeHdrFieldFind(resp_buf, resp_loc, TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE);

        if (content_range_loc) {
          DEBUG_LOG("Got TS_HTTP_STATUS_OK on cache hit or revalidation and Content-Range header present in response");
          partial_content_reason = true;
          TSHandleMLocRelease(resp_buf, resp_loc, content_range_loc);
        } else {
          DEBUG_LOG("Got TS_HTTP_STATUS_OK on cache hit and Content-Range header is NOT present in response");
        }
      } else if (txn_state->origin_status ==
                 TS_HTTP_STATUS_PARTIAL_CONTENT) { // only set on cache miss in handle_server_read_response()
        DEBUG_LOG("Got TS_HTTP_STATUS_OK with origin TS_HTTP_STATUS_PARTIAL_CONTENT");
        partial_content_reason = true;
      } else {
        DEBUG_LOG("Allowing TS_HTTP_STATUS_OK in response due to origin status code %d", txn_state->origin_status);
      }

      if (partial_content_reason) {
        DEBUG_LOG("Restoring response header to TS_HTTP_STATUS_PARTIAL_CONTENT.");
        TSHttpHdrStatusSet(resp_buf, resp_loc, TS_HTTP_STATUS_PARTIAL_CONTENT);
      }

      remove_header(resp_buf, resp_loc, SLICE_CRR_HEADER.data(), SLICE_CRR_HEADER.size());
      if (txn_state->slice_response) {
        set_header(resp_buf, resp_loc, SLICE_CRR_HEADER.data(), SLICE_CRR_HEADER.size(), SLICE_CRR_VAL.data(),
                   SLICE_CRR_VAL.size());
      }
    } else {
      DEBUG_LOG("Ignoring status code %d; txn_state->origin_status=%d", status, txn_state->origin_status);
    }
    TSHandleMLocRelease(resp_buf, TS_NULL_MLOC, resp_loc);
  }

  if (partial_content_reason) {
    DEBUG_LOG("Attempting to restore the Range header");
    std::string const &rv = txn_state->range_value;
    // Restore the range request header
    if (!rv.empty()) {
      TSMBuffer req_buf = nullptr;
      TSMLoc req_loc    = TS_NULL_MLOC;
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc)) {
        DEBUG_LOG("Adding range header: %s", rv.c_str());
        if (!set_header(req_buf, req_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rv.data(), rv.size())) {
          DEBUG_LOG("set_header() failed.");
        }
        TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
      }
    }
  }
}

/**
 * After receiving a range request response from the origin, change
 * the response status from a 206 to a 200 so that
 * the response will be written to cache.
 */
void
handle_server_read_response(TSHttpTxn txnp, txndata *const txn_state)
{
  TSMBuffer resp_buf = nullptr;
  TSMLoc resp_loc    = TS_NULL_MLOC;
  int cache_lookup;

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &resp_buf, &resp_loc)) {
    TSHttpStatus const status = TSHttpHdrStatusGet(resp_buf, resp_loc);
    txn_state->origin_status  = status;
    if (TS_HTTP_STATUS_PARTIAL_CONTENT == status) {
      DEBUG_LOG("Got TS_HTTP_STATUS_PARTIAL_CONTENT.");
      // changing the status code from 206 to 200 forces the object into cache
      TSHttpHdrStatusSet(resp_buf, resp_loc, TS_HTTP_STATUS_OK);
      DEBUG_LOG("Set response header to TS_HTTP_STATUS_OK.");

      if (txn_state->verify_cacheability && !TSHttpTxnIsCacheable(txnp, nullptr, resp_buf)) {
        DEBUG_LOG("transaction is not cacheable; resetting status code to 206");
        TSHttpHdrStatusSet(resp_buf, resp_loc, TS_HTTP_STATUS_PARTIAL_CONTENT);
      }
    } else if (TS_HTTP_STATUS_OK == status) {
      bool cacheable = txn_state->cache_complete_responses;

      if (cacheable && txn_state->verify_cacheability) {
        DEBUG_LOG("Received a cacheable complete response from the origin; verifying cacheability");
        cacheable = TSHttpTxnIsCacheable(txnp, nullptr, resp_buf);
      }

      // 200s are cached by default; only cache if configured to do so
      if (!cacheable && TS_SUCCESS == TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SERVER_NO_STORE, true)) {
        DEBUG_LOG("Cache write has been disabled for this transaction.");
      } else {
        DEBUG_LOG("Allowing object to be cached.");
      }
    }

    // slice requesting cache lookup status and cacheability (only on miss or validation)
    if ((txn_state->origin_status == TS_HTTP_STATUS_PARTIAL_CONTENT || txn_state->origin_status == TS_HTTP_STATUS_NOT_MODIFIED) &&
        txn_state->slice_request && TSHttpTxnIsCacheable(txnp, nullptr, resp_buf) &&
        TSHttpTxnCacheLookupStatusGet(txnp, &cache_lookup) == TS_SUCCESS &&
        (cache_lookup == TS_CACHE_LOOKUP_MISS || cache_lookup == TS_CACHE_LOOKUP_HIT_STALE)) {
      txn_state->slice_response = true;
    }

    TSHandleMLocRelease(resp_buf, TS_NULL_MLOC, resp_loc);
  }
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

  while (TS_NULL_MLOC != field) {
    TSMLoc const tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field);

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
  if (nullptr == buf || TS_NULL_MLOC == hdr_loc || nullptr == header || len <= 0 || nullptr == val || val_len <= 0) {
    return false;
  }

  DEBUG_LOG("header: %s, len: %d, val: %s, val_len: %d", header, len, val, val_len);
  bool ret         = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(buf, hdr_loc, header, len);

  if (TS_NULL_MLOC == field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(buf, hdr_loc, header, len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(buf, hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
    }
  } else {
    bool first = true;

    while (TS_NULL_MLOC != field_loc) {
      TSMLoc const tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field_loc);
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(buf, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  return ret;
}

time_t
get_date_from_cached_hdr(TSHttpTxn txn)
{
  TSMBuffer buf  = nullptr;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  time_t date    = 0;

  if (TSHttpTxnCachedRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
    TSMLoc const date_loc = TSMimeHdrFieldFind(buf, hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (TS_NULL_MLOC != date_loc) {
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
    TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, txnp_cont);
  }
}
