/** @file

  A brief file description

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

/*
 * cache_scan.cc:  use TSCacheScan to print URLs and headers for objects in
 *                 the cache when endpoint /show-cache is requested
 */
#include <cstdio>
#include <cstring>
#include <climits>
#include <cstdlib>

#include "ts/ts.h"
#include "ts/experimental.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "cache_scan"

static TSCont global_contp;

struct cache_scan_state_t {
  TSVConn net_vc;
  TSVConn cache_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSHttpTxn http_txnp;
  TSAction pending_action;
  TSCacheKey key_to_delete;

  int64_t total_bytes;
  int total_items;
  int done;

  bool write_pending;
};

using cache_scan_state = struct cache_scan_state_t;

//----------------------------------------------------------------------------
static int
handle_scan(TSCont contp, TSEvent event, void *edata)
{
  TSCacheHttpInfo cache_infop;
  cache_scan_state *cstate = (cache_scan_state *)TSContDataGet(contp);

  if (event == TS_EVENT_CACHE_REMOVE) {
    cstate->done       = 1;
    const char error[] = "Cache remove operation succeeded";
    cstate->cache_vc   = (TSVConn)edata;
    cstate->write_vio  = TSVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT64_MAX);
    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    TSVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    TSVIOReenable(cstate->write_vio);
    return 0;
  }

  if (event == TS_EVENT_CACHE_REMOVE_FAILED) {
    cstate->done       = 1;
    const char error[] = "Cache remove operation failed error=";
    char rc[12];
    snprintf(rc, 12, "%p", edata);
    cstate->cache_vc  = (TSVConn)edata;
    cstate->write_vio = TSVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT64_MAX);
    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, rc, strlen(rc));

    TSVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    TSVIOReenable(cstate->write_vio);
    return 0;
  }

  // first scan event, save vc and start write
  if (event == TS_EVENT_CACHE_SCAN) {
    cstate->cache_vc  = (TSVConn)edata;
    cstate->write_vio = TSVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT64_MAX);
    return TS_EVENT_CONTINUE;
  }
  // just stop scanning if blocked or failed
  if (event == TS_EVENT_CACHE_SCAN_FAILED || event == TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED ||
      event == TS_EVENT_CACHE_SCAN_OPERATION_FAILED) {
    cstate->done = 1;
    if (cstate->resp_buffer) {
      const char error[] = "Cache scan operation blocked or failed";
      cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    }
    if (cstate->write_vio) {
      TSVIONBytesSet(cstate->write_vio, cstate->total_bytes);
      TSVIOReenable(cstate->write_vio);
    }
    return TS_CACHE_SCAN_RESULT_DONE;
  }

  // grab header and print url to outgoing vio
  if (event == TS_EVENT_CACHE_SCAN_OBJECT) {
    if (cstate->done) {
      return TS_CACHE_SCAN_RESULT_DONE;
    }
    cache_infop = (TSCacheHttpInfo)edata;

    TSMBuffer req_bufp, resp_bufp;
    TSMLoc req_hdr_loc, resp_hdr_loc;
    TSMLoc url_loc;

    char *url;
    int url_len;
    const char s1[] = "URL: ", s2[] = "\n";
    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, s1, sizeof(s1) - 1);
    TSCacheHttpInfoReqGet(cache_infop, &req_bufp, &req_hdr_loc);
    if (TS_SUCCESS == TSHttpHdrUrlGet(req_bufp, req_hdr_loc, &url_loc)) {
      url = TSUrlStringGet(req_bufp, url_loc, &url_len);

      cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, url, url_len);
      cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, s2, sizeof(s2) - 1);

      TSfree(url);
      TSHandleMLocRelease(req_bufp, req_hdr_loc, url_loc);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr_loc);
    }

    // print the response headers
    TSCacheHttpInfoRespGet(cache_infop, &resp_bufp, &resp_hdr_loc);
    cstate->total_bytes += TSMimeHdrLengthGet(resp_bufp, resp_hdr_loc);
    TSMimeHdrPrint(resp_bufp, resp_hdr_loc, cstate->resp_buffer);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_hdr_loc);

    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, s2, sizeof(s2) - 1);
    if (!cstate->write_pending) {
      cstate->write_pending = true;
      TSVIOReenable(cstate->write_vio);
    }

    cstate->total_items++;
    return TS_CACHE_SCAN_RESULT_CONTINUE;
  }
  // CACHE_SCAN_DONE: ready to close the vc on the next write reenable
  if (event == TS_EVENT_CACHE_SCAN_DONE) {
    cstate->done = 1;
    char s[512];
    int s_len = snprintf(s, sizeof(s),
                         "</pre></p>\n<p>%d total objects in cache</p>\n"
                         "<form method=\"GET\" action=\"/show-cache\">"
                         "Enter URL to delete: <input type=\"text\" size=\"40\" name=\"remove_url\">"
                         "<input type=\"submit\"  value=\"Delete URL\">",
                         cstate->total_items);
    cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, s, s_len);
    TSVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    if (!cstate->write_pending) {
      cstate->write_pending = true;
      TSVIOReenable(cstate->write_vio);
    }
    return TS_CACHE_SCAN_RESULT_DONE;
  }

  TSError("[%s] Unknown event in handle_scan: %d", PLUGIN_NAME, event);
  return -1;
}

//----------------------------------------------------------------------------
static int
handle_accept(TSCont contp, TSEvent event, TSVConn vc)
{
  cache_scan_state *cstate = (cache_scan_state *)TSContDataGet(contp);

  if (event == TS_EVENT_NET_ACCEPT) {
    if (cstate) {
      // setup vc, buffers
      cstate->net_vc = vc;

      cstate->req_buffer  = TSIOBufferCreate();
      cstate->resp_buffer = TSIOBufferCreate();
      cstate->resp_reader = TSIOBufferReaderAlloc(cstate->resp_buffer);

      cstate->read_vio = TSVConnRead(cstate->net_vc, contp, cstate->req_buffer, INT64_MAX);
    } else {
      TSVConnClose(vc);
      TSContDestroy(contp);
    }
  } else {
    // net_accept failed
    if (cstate) {
      TSfree(cstate);
    }
    TSContDestroy(contp);
  }

  return 0;
}

//----------------------------------------------------------------------------
static void
cleanup(TSCont contp)
{
  // shutdown vc and free memory
  cache_scan_state *cstate = (cache_scan_state *)TSContDataGet(contp);

  if (cstate) {
    // cancel any pending cache scan actions, since we will be destroying the
    // continuation
    if (cstate->pending_action) {
      TSActionCancel(cstate->pending_action);
    }

    if (cstate->net_vc) {
      TSVConnShutdown(cstate->net_vc, 1, 1);
    }

    if (cstate->req_buffer) {
      TSIOBufferDestroy(cstate->req_buffer);
      cstate->req_buffer = nullptr;
    }

    if (cstate->key_to_delete) {
      if (TSCacheKeyDestroy(cstate->key_to_delete) == TS_ERROR) {
        TSError("[%s] Failed to destroy cache key", PLUGIN_NAME);
      }
      cstate->key_to_delete = nullptr;
    }

    if (cstate->resp_buffer) {
      TSIOBufferDestroy(cstate->resp_buffer);
      cstate->resp_buffer = nullptr;
    }

    TSVConnClose(cstate->net_vc);
    TSfree(cstate);
  }
  TSContDestroy(contp);
}

//----------------------------------------------------------------------------
static int
handle_io(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  cache_scan_state *cstate = (cache_scan_state *)TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE: {
    // we don't care about the request, so just shut down the read vc
    TSVConnShutdown(cstate->net_vc, 1, 0);
    // setup the response headers so we are ready to write body
    char hdrs[]         = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";
    cstate->total_bytes = TSIOBufferWrite(cstate->resp_buffer, hdrs, sizeof(hdrs) - 1);

    if (cstate->key_to_delete) {
      TSAction actionp = TSCacheRemove(contp, cstate->key_to_delete);
      if (!TSActionDone(actionp)) {
        cstate->pending_action = actionp;
      }
    } else {
      char head[] = "<h3>Cache Contents:</h3>\n<p><pre>\n";
      cstate->total_bytes += TSIOBufferWrite(cstate->resp_buffer, head, sizeof(head) - 1);
      // start scan
      TSAction actionp = TSCacheScan(contp, nullptr, 512000);
      if (!TSActionDone(actionp)) {
        cstate->pending_action = actionp;
      }
    }

    return 0;
  } break;
  case TS_EVENT_VCONN_WRITE_READY: {
    TSDebug(PLUGIN_NAME, "ndone: %" PRId64 " total_bytes: % " PRId64, TSVIONDoneGet(cstate->write_vio), cstate->total_bytes);
    cstate->write_pending = false;
    // the cache scan handler should call vio reenable when there is
    // available data
    // TSVIOReenable(cstate->write_vio);
    return 0;
  } break;
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSDebug(PLUGIN_NAME, "write complete");
    cstate->done = 1;
    cleanup(contp);
  } break;
  case TS_EVENT_VCONN_EOS:
  default: {
    cstate->done = 1;
    cleanup(contp);
  } break;
  }

  return 0;
}

//----------------------------------------------------------------------------
// handler for VConnection and CacheScan events
static int
cache_intercept(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "cache_intercept event: %d", event);

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
  case TS_EVENT_NET_ACCEPT_FAILED:
    return handle_accept(contp, event, (TSVConn)edata);
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    return handle_io(contp, event, edata);
  case TS_EVENT_CACHE_SCAN:
  case TS_EVENT_CACHE_SCAN_FAILED:
  case TS_EVENT_CACHE_SCAN_OBJECT:
  case TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED:
  case TS_EVENT_CACHE_SCAN_OPERATION_FAILED:
  case TS_EVENT_CACHE_SCAN_DONE:
  case TS_EVENT_CACHE_REMOVE:
  case TS_EVENT_CACHE_REMOVE_FAILED:
    return handle_scan(contp, event, edata);
  case TS_EVENT_ERROR:
    cleanup(contp);
    return 0;
  default:
    TSError("[%s] Unknown event in cache_intercept: %d", PLUGIN_NAME, event);
    cleanup(contp);
    return 0;
  }
}

// int unescapifyStr(char* buffer)
//
//   Unescapifies a URL without a making a copy.
//    The passed in string is modified
//
int
unescapifyStr(char *buffer)
{
  char *read  = buffer;
  char *write = buffer;
  char subStr[3];

  subStr[2] = '\0';
  while (*read != '\0') {
    if (*read == '%' && *(read + 1) != '\0' && *(read + 2) != '\0') {
      subStr[0] = *(++read);
      subStr[1] = *(++read);
      *write    = (char)strtol(subStr, (char **)nullptr, 16);
      read++;
      write++;
    } else if (*read == '+') {
      *write = ' ';
      write++;
      read++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';

  return (write - buffer);
}

//----------------------------------------------------------------------------
static int
setup_request(TSCont contp, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  TSCont scan_contp;
  const char *path, *query;
  cache_scan_state *cstate;
  int path_len, query_len;

  TSAssert(contp == global_contp);

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  path = TSUrlPathGet(bufp, url_loc, &path_len);
  if (!path) {
    TSError("[%s] Couldn't retrieve request path", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);

  if (path_len == 10 && !strncmp(path, "show-cache", 10)) {
    scan_contp = TSContCreate(cache_intercept, TSMutexCreate());
    TSHttpTxnIntercept(scan_contp, txnp);
    cstate = (cache_scan_state *)TSmalloc(sizeof(cache_scan_state));
    memset(cstate, 0, sizeof(cache_scan_state));
    cstate->http_txnp = txnp;

    if (query && query_len > 11) {
      char querybuf[2048];
      query_len   = (unsigned)query_len > sizeof(querybuf) - 1 ? sizeof(querybuf) - 1 : query_len;
      char *start = querybuf, *end = querybuf + query_len;
      size_t del_url_len;
      memcpy(querybuf, query, query_len);
      *end  = '\0';
      start = strstr(querybuf, "remove_url=");
      if (start && (start == querybuf || *(start - 1) == '&')) {
        start += 11;
        if ((end = strstr(start, "&")) != nullptr) {
          *end = '\0';
        }
        del_url_len = unescapifyStr(start);
        end         = start + del_url_len;

        cstate->key_to_delete = TSCacheKeyCreate();
        TSDebug(PLUGIN_NAME, "deleting url: %s", start);

        TSMBuffer urlBuf = TSMBufferCreate();
        TSMLoc urlLoc;

        if (TS_SUCCESS == TSUrlCreate(urlBuf, &urlLoc)) {
          if (TSUrlParse(urlBuf, urlLoc, (const char **)&start, end) != TS_PARSE_DONE ||
              TSCacheKeyDigestFromUrlSet(cstate->key_to_delete, urlLoc) != TS_SUCCESS) {
            TSError("[%s] CacheKeyDigestFromUrlSet failed", PLUGIN_NAME);
            TSCacheKeyDestroy(cstate->key_to_delete);
            TSfree(cstate);
            TSHandleMLocRelease(urlBuf, TS_NULL_MLOC, urlLoc);
            TSMBufferDestroy(urlBuf);
            goto Ldone;
          }
          TSHandleMLocRelease(urlBuf, TS_NULL_MLOC, urlLoc);
        } else {
          TSError("[%s] TSUrlCreate failed", PLUGIN_NAME);
        }
        TSMBufferDestroy(urlBuf);
      }
    }

    TSContDataSet(scan_contp, cstate);
    TSDebug(PLUGIN_NAME, "setup cache intercept");
  } else {
    TSDebug(PLUGIN_NAME, "not a cache iter request");
  }

Ldone:
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

//----------------------------------------------------------------------------
// handler for http txn events
static int
cache_print_plugin(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    return setup_request(contp, (TSHttpTxn)edata);
  default:
    break;
  }
  TSHttpTxnReenable((TSHttpTxn)edata, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

//----------------------------------------------------------------------------
void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    global_contp = TSContCreate(cache_print_plugin, TSMutexCreate());
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global_contp);
  } else {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }
}
