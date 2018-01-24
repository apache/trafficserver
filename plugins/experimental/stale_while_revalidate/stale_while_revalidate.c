/** @file

  Implements RFC 5861 (HTTP Cache-Control Extensions for Stale Content)

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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <search.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ts/ink_defs.h"
#include "ts/ts.h"
#include "ts/experimental.h"

#define PLUGIN_NAME "stale_while_revalidate"

#define XSTRCMP ((int (*)(const void *a, const void *b))strcmp)

static const char HTTP_VALUE_STALE_WHILE_REVALIDATE[] = "stale-while-revalidate";
static const char HTTP_VALUE_STALE_IF_ERROR[]         = "stale-if-error";
static const char HTTP_VALUE_STALE_WARNING[]          = "110 Response is stale";

#define STALE_WARNING_CODE 110

typedef struct {
  TSTextLogObject object;
  bool all, stale_if_error, stale_while_revalidate;
  char *filename;
} log_info_t;

typedef struct {
  void *troot;
  TSMutex troot_mutex;
  int txn_slot;
  time_t stale_if_error_override;
  log_info_t log_info;
} config_t;

typedef struct {
  time_t date, stale_while_revalidate, stale_on_error, max_age, rmt_date;
} CachedHeaderInfo;

typedef struct {
  char *effective_url;
  TSMBuffer buf;
  TSMLoc http_hdr_loc;
  union {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
  } client_addr;
} RequestInfo;

typedef struct {
  TSHttpTxn txn;
  bool async_req;
  TSIOBuffer req_io_buf, resp_io_buf;
  TSIOBufferReader req_io_buf_reader, resp_io_buf_reader;
  TSVIO r_vio, w_vio;
  TSVConn vconn;
  RequestInfo req_info_obj, *req_info;
  TSHttpStatus rmt_resp_status;
  time_t rmt_resp_date;
  time_t txn_start;
  config_t *plugin_config;
} StateInfo;

static RequestInfo *
init_request_info(RequestInfo *pobj, TSHttpTxn txn)
{
  RequestInfo *req_info = NULL;
  char *url;
  int url_len;
  TSMBuffer buf;
  TSMLoc loc;

  const struct sockaddr *sa;

  if (TSHttpTxnClientReqGet(txn, &buf, &loc) == TS_SUCCESS) {
    req_info = pobj;
    memset(req_info, 0, sizeof(RequestInfo));

    url                     = TSHttpTxnEffectiveUrlStringGet(txn, &url_len);
    req_info->effective_url = TSstrndup(url, url_len);
    TSfree(url);

    req_info->buf = TSMBufferCreate();
    TSHttpHdrClone(req_info->buf, buf, loc, &(req_info->http_hdr_loc));
    TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);

    sa = TSHttpTxnClientAddrGet(txn);
    switch (sa->sa_family) {
    case AF_INET:
      memcpy(&req_info->client_addr.sin, sa, sizeof(struct sockaddr_in));
      break;
    case AF_INET6:
      memcpy(&req_info->client_addr.sin6, sa, sizeof(struct sockaddr_in6));
      break;
    default:
      break;
    }
  }

  return req_info;
}

static void
free_request_info(RequestInfo *req_info)
{
  TSfree(req_info->effective_url);
  TSHandleMLocRelease(req_info->buf, TS_NULL_MLOC, req_info->http_hdr_loc);
  TSMBufferDestroy(req_info->buf);
}

static void
free_request_state(StateInfo *state)
{
#if defined(DEBUG)
  int verify = 1;
#else
  int verify = TSIsDebugTagSet(PLUGIN_NAME);
#endif

  // Verify that the effective URL of this state object has been removed before we delete the state.
  if (verify) {
    void *ptr;

    TSMutexLock(state->plugin_config->troot_mutex);
    ptr = tfind(state->req_info->effective_url, &(state->plugin_config->troot), XSTRCMP);
    TSMutexUnlock(state->plugin_config->troot_mutex);

    if (ptr) {
      TSReleaseAssert(ptr != state->req_info->effective_url);
    }
  }

  free_request_info(state->req_info);

  state->req_info = NULL;

  TSfree(state);
}

static CachedHeaderInfo *
get_cached_header_info(CachedHeaderInfo *chi, TSHttpTxn txn)
{
  TSMBuffer cr_buf;
  TSMLoc cr_hdr_loc, cr_date_loc, cr_cache_control_loc, cr_cache_control_dup_loc;
  int cr_cache_control_count, val_len, i;
  char *value, *ptr;

  memset(chi, 0, sizeof(CachedHeaderInfo));

  TSHttpTxnCachedRespTimeGet(txn, &chi->date); // for local-clock age

  if (TSHttpTxnCachedRespGet(txn, &cr_buf, &cr_hdr_loc) == TS_SUCCESS) {
    cr_date_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (cr_date_loc != TS_NULL_MLOC) {
      TSDebug(PLUGIN_NAME, "Found a date");
      chi->rmt_date = TSMimeHdrFieldValueDateGet(cr_buf, cr_hdr_loc, cr_date_loc);
      TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_date_loc);
    }

    cr_cache_control_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);

    while (cr_cache_control_loc != TS_NULL_MLOC) {
      TSDebug(PLUGIN_NAME, "Found cache-control");
      cr_cache_control_count = TSMimeHdrFieldValuesCount(cr_buf, cr_hdr_loc, cr_cache_control_loc);

      for (i = 0; i < cr_cache_control_count; i++) {
        value = (char *)TSMimeHdrFieldValueStringGet(cr_buf, cr_hdr_loc, cr_cache_control_loc, i, &val_len);
        ptr   = value;

        if (strncmp(value, TS_HTTP_VALUE_MAX_AGE, TS_HTTP_LEN_MAX_AGE) == 0) {
          TSDebug(PLUGIN_NAME, "Found max-age");
          ptr += TS_HTTP_LEN_MAX_AGE;
          if (*ptr == '=') {
            ptr++;
            chi->max_age = atol(ptr);
          } else {
            ptr = TSstrndup(value, TS_HTTP_LEN_MAX_AGE + 2);
            TSDebug(PLUGIN_NAME, "Parse max-age failed: %s", ptr);
            TSfree(ptr);
          }
        } else if (strncmp(value, HTTP_VALUE_STALE_WHILE_REVALIDATE, strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE)) == 0) {
          TSDebug(PLUGIN_NAME, "Found stale-while-revalidate");
          ptr += strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE);
          if (*ptr == '=') {
            ptr++;
            chi->stale_while_revalidate = atol(ptr);
          }
        } else if (strncmp(value, HTTP_VALUE_STALE_IF_ERROR, strlen(HTTP_VALUE_STALE_IF_ERROR)) == 0) {
          TSDebug(PLUGIN_NAME, "Found stale-on-error");
          ptr += strlen(HTTP_VALUE_STALE_IF_ERROR);
          if (*ptr == '=') {
            ptr++;
            chi->stale_on_error = atol(ptr);
          }
        } else {
          TSDebug(PLUGIN_NAME, "Unknown field value");
        }
      }

      cr_cache_control_dup_loc = TSMimeHdrFieldNextDup(cr_buf, cr_hdr_loc, cr_cache_control_loc);
      TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_cache_control_loc);
      cr_cache_control_loc = cr_cache_control_dup_loc;
    }
    TSHandleMLocRelease(cr_buf, TS_NULL_MLOC, cr_hdr_loc);
  }

  return chi;
}

static int
parse_status_code(TSIOBufferReader reader)
{
  TSIOBufferBlock block;
  int64_t avail = 0;
  char *start   = NULL;
  char *srch;

  block = TSIOBufferReaderStart(reader);

  if (block) {
    start = (char *)TSIOBufferBlockReadStart(block, reader, &avail);
  }

  // needs { ... <sp> 3*<digit> ... <nl>} in buffer
  if (!start || !(srch = memchr(start, '\n', avail)) || !(srch = memchr(start, ' ', srch - start)) || srch + 5 > start + avail) {
    return -1; // cannot find valid status line...
  }

  // first line / first space is before resp status
  int status = atoi(srch + 1);
  if (status < 100 || status > 699) {
    return -1; // ignore if malformed
  }
  return status;
}

static void
consume_data(StateInfo *state)
{
  int64_t avail;

  // sync-bgd-check only
  if (!state->rmt_resp_status) {
    state->rmt_resp_status = parse_status_code(state->resp_io_buf_reader);
    TSDebug(PLUGIN_NAME, "%p: Tunnel HTTP Status: %d", state->txn, state->rmt_resp_status);
  }

  // Consume data correctly
  avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);
  TSIOBufferReaderConsume(state->resp_io_buf_reader, avail);
  TSVIONDoneSet(state->r_vio, TSVIONDoneGet(state->r_vio) + avail);
}

static int
consume_resource(TSCont cont, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  StateInfo *state;
  TSVConn vconn;

  vconn = (TSVConn)edata;
  state = (StateInfo *)TSContDataGet(cont);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    // We shouldn't get here because we specify the exact size of the buffer.
    TSDebug(PLUGIN_NAME, "%p: Write Ready", state->txn);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(PLUGIN_NAME, "%p: Write Complete", state->txn);
    // TSDebug(PLUGIN_NAME, "TSVConnShutdown()");
    // TSVConnShutdown(state->vconn, 0, 1);
    // TSVIOReenable(state->w_vio);
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSDebug(PLUGIN_NAME, "%p: Read Ready", state->txn);
    consume_data(state);
    TSVIOReenable(state->r_vio);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      TSDebug(PLUGIN_NAME, "%p: Inactivity Timeout", state->txn);
      TSVConnAbort(vconn, TS_VC_CLOSE_ABORT);
    } else {
      if (event == TS_EVENT_VCONN_READ_COMPLETE) {
        TSDebug(PLUGIN_NAME, "%p: Read Complete", state->txn);
      } else if (event == TS_EVENT_VCONN_EOS) {
        TSDebug(PLUGIN_NAME, "%p: EOS", state->txn);
      }
      TSVConnClose(state->vconn);
    }

    consume_data(state);

    TSIOBufferReaderFree(state->req_io_buf_reader);
    TSIOBufferDestroy(state->req_io_buf);
    TSIOBufferReaderFree(state->resp_io_buf_reader);
    TSIOBufferDestroy(state->resp_io_buf);

    state->req_io_buf_reader  = NULL;
    state->req_io_buf         = NULL;
    state->resp_io_buf_reader = NULL;
    state->resp_io_buf        = NULL;

    TSContDestroy(cont); // events are done
    cont = NULL;

    // async revalidate done?
    if (state->async_req) {
      TSDebug(PLUGIN_NAME, "%p: Unlock URL", state->txn);
      TSMutexLock(state->plugin_config->troot_mutex);
      tdelete(state->req_info->effective_url, &(state->plugin_config->troot), XSTRCMP);
      TSMutexUnlock(state->plugin_config->troot_mutex);

      free_request_state(state); // async final free
      return 0;
      //////////////// RETURN (async done)
    }

    switch (state->rmt_resp_status) {
    case TS_HTTP_STATUS_INTERNAL_SERVER_ERROR:
    case TS_HTTP_STATUS_BAD_GATEWAY:
    case TS_HTTP_STATUS_SERVICE_UNAVAILABLE:
    case TS_HTTP_STATUS_GATEWAY_TIMEOUT:

      TSDebug(PLUGIN_NAME, "%p: Stale-if-error %d allows re-use", state->txn, state->rmt_resp_status);

      if (state->plugin_config->log_info.object &&
          (state->plugin_config->log_info.all || state->plugin_config->log_info.stale_if_error)) {
        CachedHeaderInfo chi_obj;
        CachedHeaderInfo *chi = get_cached_header_info(&chi_obj, state->txn);
        TSTextLogObjectWrite(state->plugin_config->log_info.object, "stale-if-error: %ld - %ld < %ld + %ld %s", state->txn_start,
                             chi->date, chi->max_age, chi->stale_on_error, state->req_info->effective_url);
      }
      state->rmt_resp_status = STALE_WARNING_CODE; // warning needed
      TSHttpTxnCacheLookupStatusSet(state->txn, TS_CACHE_LOOKUP_HIT_FRESH);
      break;

    case TS_HTTP_STATUS_NOT_MODIFIED: // can change to fresh [w/client's hdrs]
      TSDebug(PLUGIN_NAME, "%p: Revalidate successful", state->txn);
      TSHttpTxnCacheLookupStatusSet(state->txn, TS_CACHE_LOOKUP_HIT_FRESH);
      break;

    default:
      TSDebug(PLUGIN_NAME, "%p: Asset is invalid/stale", state->txn);
      break; // stay with TS_CACHE_LOOKUP_HIT_STALE
    }
    TSHttpTxnReenable(state->txn, TS_EVENT_HTTP_CONTINUE); // unblock txn
    break;
  default:
    TSError("[%s] Unknown event %d", PLUGIN_NAME, event);
    break;
  }

  return 0;
}

static void
override_hdr_field(TSMBuffer buffp, TSMLoc hdr_loc, const char *wksField, unsigned wksFieldLen, const char *str, unsigned len)
{
  TSMLoc fld_loc = TSMimeHdrFieldFind(buffp, hdr_loc, wksField, wksFieldLen);

  while (fld_loc != TS_NULL_MLOC) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(buffp, hdr_loc, fld_loc);
    TSMimeHdrFieldRemove(buffp, hdr_loc, fld_loc);
    TSMimeHdrFieldDestroy(buffp, hdr_loc, fld_loc);
    TSHandleMLocRelease(buffp, hdr_loc, fld_loc);
    fld_loc = tmp;
  }

  TSMimeHdrFieldCreateNamed(buffp, hdr_loc, wksField, wksFieldLen, &fld_loc);
  TSMimeHdrFieldValueStringSet(buffp, hdr_loc, fld_loc, -1, str, len);
  TSMimeHdrFieldAppend(buffp, hdr_loc, fld_loc);
  TSHandleMLocRelease(buffp, hdr_loc, fld_loc);
}

static int
fetch_resource(TSCont cont, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  StateInfo *state;
  TSCont consume_cont;

  state = (StateInfo *)TSContDataGet(cont);
  TSContDestroy(cont);
  cont = NULL;

  // li = (RequestInfo *) edata;
  if (state->async_req) {
    TSMutex mtx = state->plugin_config->troot_mutex;
    TSMutexLock(mtx);

    // If already doing async lookup lets just close shop and go home
    if (tfind(state->req_info->effective_url, &(state->plugin_config->troot), XSTRCMP) != NULL) {
      TSDebug(PLUGIN_NAME, "Looks like an async is already in progress");
      TSMutexUnlock(mtx);

      // async final free
      free_request_state(state);
      return 0;
      //////////////// RETURN (async-waiting)
    }
    // Otherwise lets do the lookup!
    {
      // Lock in tree
      TSDebug(PLUGIN_NAME, "%p: Locking URL", state->txn);
      tsearch(state->req_info->effective_url, &(state->plugin_config->troot), XSTRCMP);
    }

    TSMutexUnlock(mtx);
  }

  {
    TSDebug(PLUGIN_NAME, "%p: Lets do the lookup", state->txn);
    consume_cont = TSContCreate(consume_resource, TSMutexCreate());
    TSContDataSet(consume_cont, (void *)state);

    {
      char date[64];
      int datelen = 0;
      // If-Modified-Since: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
      static const char *const fmt = "%A, %d %b %Y %T GMT";

      datelen = strftime(date, sizeof(date), fmt, gmtime(&state->rmt_resp_date));

      override_hdr_field(state->req_info->buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION, TS_MIME_LEN_CONNECTION,
                         "close", strlen("close"));
      override_hdr_field(state->req_info->buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_IF_MODIFIED_SINCE,
                         TS_MIME_LEN_IF_MODIFIED_SINCE, date, datelen);
    }

    // only revalidate or detect a server error ... without a full download
    TSHttpHdrMethodSet(state->req_info->buf, state->req_info->http_hdr_loc, TS_HTTP_METHOD_HEAD, -1);

    state->req_io_buf         = TSIOBufferCreate();
    state->req_io_buf_reader  = TSIOBufferReaderAlloc(state->req_io_buf);
    state->resp_io_buf        = TSIOBufferCreate();
    state->resp_io_buf_reader = TSIOBufferReaderAlloc(state->resp_io_buf);

    TSHttpHdrPrint(state->req_info->buf, state->req_info->http_hdr_loc, state->req_io_buf);
    TSIOBufferWrite(state->req_io_buf, "\r\n", 2);

    state->vconn = TSHttpConnectWithPluginId(&state->req_info->client_addr.sa, PLUGIN_NAME, 0);

    state->r_vio = TSVConnRead(state->vconn, consume_cont, state->resp_io_buf, INT64_MAX);
    state->w_vio =
      TSVConnWrite(state->vconn, consume_cont, state->req_io_buf_reader, TSIOBufferReaderAvail(state->req_io_buf_reader));
  }

  return 0;
}

static int
is_swr_transaction(TSHttpTxn txn)
{
  const char *tag = TSHttpTxnPluginTagGet(txn);
  return tag && strcmp(tag, PLUGIN_NAME) == 0;
}

static int
main_plugin(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  int status;
  TSCont fetch_cont;
  StateInfo *state;
  TSMBuffer buf;
  TSMLoc loc, warn_loc;
  config_t *plugin_config;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:

    if (is_swr_transaction(txn)) {
      TSHttpTxnHookAdd(txn, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
    } else {
      state = TSmalloc(sizeof(StateInfo));
      memset(state, 0, sizeof(StateInfo));

      state->plugin_config = (config_t *)TSContDataGet(cont);
      state->req_info      = init_request_info(&state->req_info_obj, txn);
      state->txn           = txn;
      if (state->req_info != NULL) {
        time(&state->txn_start);

        TSHttpTxnArgSet(txn, state->plugin_config->txn_slot, (void *)state);
        TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
        TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, cont);

        TSDebug(PLUGIN_NAME, "tracking state %p from txn %p for %s", state, txn, state->req_info->effective_url);
      } else {
        free_request_state(state);
      }
    }

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:

    plugin_config = (config_t *)TSContDataGet(cont);
    state         = (StateInfo *)TSHttpTxnArgGet(txn, plugin_config->txn_slot);

    // NOTE: It is possible to receive the cache lookup
    // hook more than once if there is contention on the
    // cache write lock. Just bail if we already nuked
    // the StateInfo.
    if (state == NULL) {
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      break;
    }

    if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "cache status %d from txn %p state %p", status, txn, state);

      // Are we stale?
      if (status == TS_CACHE_LOOKUP_HIT_STALE) {
        CachedHeaderInfo chi_obj;
        CachedHeaderInfo *chi;

        TSDebug(PLUGIN_NAME, "CacheLookupStatus is STALE");
        // Get headers
        chi                  = get_cached_header_info(&chi_obj, txn);
        state->rmt_resp_date = chi->rmt_date;

        if (state->plugin_config->stale_if_error_override > chi->stale_on_error) {
          chi->stale_on_error = state->plugin_config->stale_if_error_override;
        }

        if ((state->txn_start - chi->date) < (chi->max_age + chi->stale_while_revalidate)) {
          TSDebug(PLUGIN_NAME, "Looks like we can return fresh info and validate in the background");
          if (state->plugin_config->log_info.object &&
              (state->plugin_config->log_info.all || state->plugin_config->log_info.stale_while_revalidate)) {
            TSTextLogObjectWrite(state->plugin_config->log_info.object, "stale-while-revalidate: %d - %d < %d + %d %s",
                                 (int)state->txn_start, (int)chi->date, (int)chi->max_age, (int)chi->stale_while_revalidate,
                                 state->req_info->effective_url);
          }

          TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE, 1);
          TSHttpTxnCacheLookupStatusSet(txn, TS_CACHE_LOOKUP_HIT_FRESH);

          TSDebug(PLUGIN_NAME, "set state as async");
          // cannot free with main Txn --> disable TXN_CLOSE free
          TSHttpTxnArgSet(txn, state->plugin_config->txn_slot, NULL);
          state->async_req = true;

          // Set warning header
          state->rmt_resp_status = STALE_WARNING_CODE; // stale-warning is needed
          TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);

          fetch_cont = TSContCreate(fetch_resource, TSMutexCreate());
          TSContDataSet(fetch_cont, (void *)state);
          TSContSchedule(fetch_cont, 0, TS_THREAD_POOL_NET);
          TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
        } else if ((state->txn_start - chi->date) < (chi->max_age + chi->stale_on_error)) {
          TSDebug(PLUGIN_NAME, "Looks like we can return fresh data on 500 error");
          TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE, 1);

          // lookup sync
          state->async_req = false;
          TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont); // might need warning
          fetch_cont = TSContCreate(fetch_resource, TSMutexCreate());
          TSContDataSet(fetch_cont, (void *)state);
          TSContSchedule(fetch_cont, 0, TS_THREAD_POOL_NET);
          // NOTE: only path to leave TXN blocked without TxnReenable() call
        } else {
          TSDebug(PLUGIN_NAME, "No love? now: %d date: %d max-age: %d swr: %d soe: %d", (int)state->txn_start, (int)chi->date,
                  (int)chi->max_age, (int)chi->stale_while_revalidate, (int)chi->stale_on_error);
          TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
        }
      } else {
        TSDebug(PLUGIN_NAME, "Not Stale!");
        TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      }
    } else {
      TSDebug(PLUGIN_NAME, "Could not get CacheLookupStatus");
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    }
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    // In this continuation we only hook responses for background
    // requests that we issued ourselves. If the origin went away,
    // we don't want to replace a stale cache object with an error.
    TSAssert(is_swr_transaction(txn));

    if (TS_SUCCESS == TSHttpTxnServerRespGet(txn, &buf, &loc)) {
      switch (TSHttpHdrStatusGet(buf, loc)) {
      case TS_HTTP_STATUS_INTERNAL_SERVER_ERROR:
      case TS_HTTP_STATUS_BAD_GATEWAY:
      case TS_HTTP_STATUS_SERVICE_UNAVAILABLE:
      case TS_HTTP_STATUS_GATEWAY_TIMEOUT:
        TSDebug(PLUGIN_NAME, "marking background request no-store");
        TSHttpTxnServerRespNoStoreSet(txn, 1);
        break;
      default:
        break;
      }

      TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
    }

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    plugin_config = (config_t *)TSContDataGet(cont);
    state         = (StateInfo *)TSHttpTxnArgGet(txn, plugin_config->txn_slot);

    if (!state || state->rmt_resp_status != STALE_WARNING_CODE) {
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      break;
    }

    TSDebug(PLUGIN_NAME, "set warning header");
    if (TSHttpTxnClientRespGet(txn, &buf, &loc) == TS_SUCCESS) {
      TSMimeHdrFieldCreateNamed(buf, loc, TS_MIME_FIELD_WARNING, TS_MIME_LEN_WARNING, &warn_loc);
      TSMimeHdrFieldValueStringInsert(buf, loc, warn_loc, -1, HTTP_VALUE_STALE_WARNING, strlen(HTTP_VALUE_STALE_WARNING));
      TSMimeHdrFieldAppend(buf, loc, warn_loc);
      TSHandleMLocRelease(buf, loc, warn_loc);
      TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
    } else {
      TSError("[%s] Error while getting response from txn", PLUGIN_NAME);
    }
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    plugin_config = (config_t *)TSContDataGet(cont);
    state         = (StateInfo *)TSHttpTxnArgGet(txn, plugin_config->txn_slot);
    if (state) {
      free_request_state(state);
    }
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  config_t *plugin_config;
  TSPluginRegistrationInfo info;
  TSCont main_cont;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.\n", PLUGIN_NAME);

    return;
  } else {
    TSDebug(PLUGIN_NAME, "Plugin registration succeeded");
  }

  if (argc > 1) {
    plugin_config = TSmalloc(sizeof(config_t));

    plugin_config->troot                           = NULL;
    plugin_config->troot_mutex                     = TSMutexCreate();
    plugin_config->stale_if_error_override         = 0;
    plugin_config->log_info.object                 = NULL;
    plugin_config->log_info.all                    = false;
    plugin_config->log_info.stale_if_error         = false;
    plugin_config->log_info.stale_while_revalidate = false;
    plugin_config->log_info.filename               = PLUGIN_NAME;

    int c;
    static const struct option longopts[] = {{"log-all", no_argument, NULL, 'a'},
                                             {"log-stale-while-revalidate", no_argument, NULL, 'r'},
                                             {"log-stale-if-error", no_argument, NULL, 'e'},
                                             {"log-filename", required_argument, NULL, 'f'},
                                             {"force-stale-if-error", required_argument, NULL, 'E'},
                                             {NULL, 0, NULL, 0}};

    while ((c = getopt_long(argc, (char *const *)argv, "aref:E:", longopts, NULL)) != -1) {
      switch (c) {
      case 'a':
        plugin_config->log_info.all = true;
        break;
      case 'r':
        plugin_config->log_info.stale_while_revalidate = true;
        break;
      case 'e':
        plugin_config->log_info.stale_if_error = true;
        break;
      case 'f':
        plugin_config->log_info.filename = strdup(optarg);
        break;
      case 'E':
        plugin_config->stale_if_error_override = atoi(optarg);
        break;
      default:
        break;
      }
    }

    if (plugin_config->log_info.all || plugin_config->log_info.stale_while_revalidate || plugin_config->log_info.stale_if_error) {
      if (TSTextLogObjectCreate(plugin_config->log_info.filename, TS_LOG_MODE_ADD_TIMESTAMP, &(plugin_config->log_info.object)) !=
          TS_SUCCESS) {
        TSError("[%s] Error getting the URL from the transaction", PLUGIN_NAME);
        TSfree(plugin_config);
        return;
      }
    }

    // proxy.config.http.insert_age_in_response
    TSHttpTxnArgIndexReserve(PLUGIN_NAME, "txn state info", &(plugin_config->txn_slot));
    main_cont = TSContCreate(main_plugin, NULL);
    TSContDataSet(main_cont, (void *)plugin_config);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, main_cont);

    TSDebug(PLUGIN_NAME, "Plugin Init Complete");
  }
}
