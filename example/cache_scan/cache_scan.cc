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
 * cache_scan.cc:  use INKCacheScan to print URLs and headers for objects in
 *                 the cache when endpoint /show-cache is requested
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <ts/ts.h>
#include <ts/experimental.h>

static INKCont global_contp;

struct cache_scan_state_t
{
  INKVConn net_vc;
  INKVConn cache_vc;
  INKVIO read_vio;
  INKVIO write_vio;

  INKIOBuffer req_buffer;
  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  INKHttpTxn http_txnp;
  INKAction pending_action;
  INKCacheKey key_to_delete;

  int64 total_bytes;
  int total_items;
  int done;

  bool write_pending;
};

typedef struct cache_scan_state_t cache_scan_state;


//----------------------------------------------------------------------------
static int
handle_scan(INKCont contp, INKEvent event, void *edata)
{
  INKCacheHttpInfo cache_infop;
  cache_scan_state *cstate = (cache_scan_state *) INKContDataGet(contp);

  if (event == INK_EVENT_CACHE_REMOVE) {
    cstate->done = 1;
    const char error[] = "Cache remove operation succeeded";
    cstate->cache_vc = (INKVConn) edata;
    cstate->write_vio = INKVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT_MAX);
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    INKVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    INKVIOReenable(cstate->write_vio);
    return 0;
  }

  if (event == INK_EVENT_CACHE_REMOVE_FAILED) {
    cstate->done = 1;
    const char error[] = "Cache remove operation failed error=";
    char rc[12];
    snprintf(rc, 12, "%p", edata);
    cstate->cache_vc = (INKVConn) edata;
    cstate->write_vio = INKVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT_MAX);
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, rc, strlen(rc));

    INKVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    INKVIOReenable(cstate->write_vio);
    return 0;
  }

  //first scan event, save vc and start write
  if (event == INK_EVENT_CACHE_SCAN) {
    cstate->cache_vc = (INKVConn) edata;
    cstate->write_vio = INKVConnWrite(cstate->net_vc, contp, cstate->resp_reader, INT_MAX);
    return INK_EVENT_CONTINUE;
  }
  //just stop scanning if blocked or failed
  if (event == INK_EVENT_CACHE_SCAN_FAILED ||
      event == INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED || event == INK_EVENT_CACHE_SCAN_OPERATION_FAILED) {
    cstate->done = 1;
    if (cstate->resp_buffer) {
      const char error[] = "Cache scan operation blocked or failed";
      cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, error, sizeof(error) - 1);
    }
    if (cstate->write_vio) {
      INKVIONBytesSet(cstate->write_vio, cstate->total_bytes);
      INKVIOReenable(cstate->write_vio);
    }
    return INK_CACHE_SCAN_RESULT_DONE;
  }

  //grab header and print url to outgoing vio
  if (event == INK_EVENT_CACHE_SCAN_OBJECT) {
    if (cstate->done) {
      return INK_CACHE_SCAN_RESULT_DONE;
    }
    cache_infop = (INKCacheHttpInfo) edata;

    INKMBuffer req_bufp, resp_bufp;
    INKMLoc req_hdr_loc, resp_hdr_loc;
    INKMLoc url_loc;

    char *url;
    int url_len;
    const char s1[] = "URL: ", s2[] = "\n";
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, s1, sizeof(s1) - 1);
    INKCacheHttpInfoReqGet(cache_infop, &req_bufp, &req_hdr_loc);
    url_loc = INKHttpHdrUrlGet(req_bufp, req_hdr_loc);
    url = INKUrlStringGet(req_bufp, url_loc, &url_len);

    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, url, url_len);
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, s2, sizeof(s2) - 1);

    INKfree(url);
    INKHandleMLocRelease(req_bufp, req_hdr_loc, url_loc);
    INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_hdr_loc);


    //print the response headers
    INKCacheHttpInfoRespGet(cache_infop, &resp_bufp, &resp_hdr_loc);
    cstate->total_bytes += INKMimeHdrLengthGet(resp_bufp, resp_hdr_loc);
    INKMimeHdrPrint(resp_bufp, resp_hdr_loc, cstate->resp_buffer);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_hdr_loc);


    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, s2, sizeof(s2) - 1);
    if (!cstate->write_pending) {
      cstate->write_pending = 1;
      INKVIOReenable(cstate->write_vio);
    }

    cstate->total_items++;
    return INK_CACHE_SCAN_RESULT_CONTINUE;
  }
  //CACHE_SCAN_DONE: ready to close the vc on the next write reenable
  if (event == INK_EVENT_CACHE_SCAN_DONE) {
    cstate->done = 1;
    char s[512];
    int s_len = snprintf(s, sizeof(s),
                         "</pre></p>\n<p>%d total objects in cache</p>\n"
                         "<form method=\"GET\" action=\"/show-cache\">"
                         "Enter URL to delete: <input type=\"text\" size=\"40\" name=\"remove_url\">"
                         "<input type=\"submit\"  value=\"Delete URL\">",
                         cstate->total_items);
    cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, s, s_len);
    INKVIONBytesSet(cstate->write_vio, cstate->total_bytes);
    if (!cstate->write_pending) {
      cstate->write_pending = 1;
      INKVIOReenable(cstate->write_vio);
    }
    return INK_CACHE_SCAN_RESULT_DONE;
  }

  INKError("Unknown event in handle_scan: %d", event);
  return -1;
}

//----------------------------------------------------------------------------
static int
handle_accept(INKCont contp, INKEvent event, INKVConn vc)
{
  cache_scan_state *cstate = (cache_scan_state *) INKContDataGet(contp);

  if (event == INK_EVENT_NET_ACCEPT) {
    if (cstate) {
      //setup vc, buffers
      cstate->net_vc = vc;

      cstate->req_buffer = INKIOBufferCreate();
      cstate->resp_buffer = INKIOBufferCreate();
      cstate->resp_reader = INKIOBufferReaderAlloc(cstate->resp_buffer);

      cstate->read_vio = INKVConnRead(cstate->net_vc, contp, cstate->req_buffer, INT_MAX);
    } else {
      INKVConnClose(vc);
      INKContDestroy(contp);
    }
  } else {
    //net_accept failed
    if (cstate) {
      INKfree(cstate);
    }
    INKContDestroy(contp);
  }

  return 0;
}

//----------------------------------------------------------------------------
static void
cleanup(INKCont contp)
{

  //shutdown vc and free memory
  cache_scan_state *cstate = (cache_scan_state *) INKContDataGet(contp);

  if (cstate) {
    // cancel any pending cache scan actions, since we will be destroying the
    // continuation
    if (cstate->pending_action) {
      INKActionCancel(cstate->pending_action);
    }

    if (cstate->net_vc) {
      INKVConnShutdown(cstate->net_vc, 1, 1);
    }

    if (cstate->req_buffer) {
      if (INKIOBufferDestroy(cstate->req_buffer) == INK_ERROR) {
        INKError("failed to destroy req_buffer");
      }
      cstate->req_buffer = NULL;
    }

    if (cstate->key_to_delete) {
      if (INKCacheKeyDestroy(cstate->key_to_delete) == INK_ERROR) {
        INKError("failed to destroy cache key");
      }
      cstate->key_to_delete = NULL;
    }

    if (cstate->resp_buffer) {
      if (INKIOBufferDestroy(cstate->resp_buffer) == INK_ERROR) {
        INKError("failed to destroy resp_buffer");
      }
      cstate->resp_buffer = NULL;
    }

    if (INKVConnClose(cstate->net_vc) == INK_ERROR) {
      INKError("INKVConnClose failed");
    }

    INKfree(cstate);
  }
  INKContDestroy(contp);
}

//----------------------------------------------------------------------------
static int
handle_io(INKCont contp, INKEvent event, void *edata)
{
  cache_scan_state *cstate = (cache_scan_state *) INKContDataGet(contp);

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
  case INK_EVENT_VCONN_READ_COMPLETE:
    {
      //we don't care about the request, so just shut down the read vc
      if (INKVConnShutdown(cstate->net_vc, 1, 0) == INK_ERROR) {
        INKError("INKVConnShutdown failed");
        cleanup(contp);
        return 0;
      }
      //setup the response headers so we are ready to write body
      char hdrs[] = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";
      cstate->total_bytes = INKIOBufferWrite(cstate->resp_buffer, hdrs, sizeof(hdrs) - 1);

      if (cstate->key_to_delete) {
        INKAction actionp = INKCacheRemove(contp, cstate->key_to_delete);
        if (actionp != INK_ERROR_PTR) {
          if (!INKActionDone(actionp)) {
            cstate->pending_action = actionp;
          }
        } else {
          INKError("CacheRemove action failed");
          cleanup(contp);
          return 0;
        }
      } else {
        char head[] = "<h3>Cache Contents:</h3>\n<p><pre>\n";
        cstate->total_bytes += INKIOBufferWrite(cstate->resp_buffer, head, sizeof(head) - 1);
        //start scan
        INKAction actionp = INKCacheScan(contp, 0, 512000);
        if (actionp != INK_ERROR_PTR) {
          if (!INKActionDone(actionp)) {
            cstate->pending_action = actionp;
          }
        } else {
          INKError("CacheScan action failed");
          cleanup(contp);
          return 0;
        }
      }

      return 0;
    }
  case INK_EVENT_VCONN_WRITE_READY:
    {
      INKDebug("cache_iter", "ndone: %d total_bytes: %d", INKVIONDoneGet(cstate->write_vio), cstate->total_bytes);
      cstate->write_pending = 0;
      // the cache scan handler should call vio reenable when there is
      // available data
      //INKVIOReenable(cstate->write_vio);
      return 0;
    }
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug("cache_iter", "write complete");
  case INK_EVENT_VCONN_EOS:
  default:
    cstate->done = 1;
    cleanup(contp);
  }

  return 0;
}


//----------------------------------------------------------------------------
// handler for VConnection and CacheScan events
static int
cache_intercept(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_iter", "cache_intercept event: %d", event);

  switch (event) {
  case INK_EVENT_NET_ACCEPT:
  case INK_EVENT_NET_ACCEPT_FAILED:
    return handle_accept(contp, event, (INKVConn) edata);
  case INK_EVENT_VCONN_READ_READY:
  case INK_EVENT_VCONN_READ_COMPLETE:
  case INK_EVENT_VCONN_WRITE_READY:
  case INK_EVENT_VCONN_WRITE_COMPLETE:
  case INK_EVENT_VCONN_EOS:
    return handle_io(contp, event, edata);
  case INK_EVENT_CACHE_SCAN:
  case INK_EVENT_CACHE_SCAN_FAILED:
  case INK_EVENT_CACHE_SCAN_OBJECT:
  case INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED:
  case INK_EVENT_CACHE_SCAN_OPERATION_FAILED:
  case INK_EVENT_CACHE_SCAN_DONE:
  case INK_EVENT_CACHE_REMOVE:
  case INK_EVENT_CACHE_REMOVE_FAILED:
    return handle_scan(contp, event, edata);
  case INK_EVENT_ERROR:
    cleanup(contp);
    return 0;
  default:
    INKError("Unknown event in cache_intercept: %d", event);
    cleanup(contp);
    return 0;
  }
}

// void unescapifyStr(char* buffer)
//
//   Unescapifies a URL without a making a copy.
//    The passed in string is modified
//
void
unescapifyStr(char *buffer)
{
  char *read = buffer;
  char *write = buffer;
  char subStr[3];
  long charVal;

  subStr[2] = '\0';
  while (*read != '\0') {
    if (*read == '%' && *(read + 1) != '\0' && *(read + 2) != '\0') {
      subStr[0] = *(++read);
      subStr[1] = *(++read);
      charVal = strtol(subStr, (char **) NULL, 16);
      *write = (char) charVal;
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
}


//----------------------------------------------------------------------------
static int
setup_request(INKCont contp, INKHttpTxn txnp)
{

  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  INKCont scan_contp;
  const char *path, *query;
  cache_scan_state *cstate;
  int path_len, query_len;

  INKAssert(contp == global_contp);

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header");
    return INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  }

  path = INKUrlPathGet(bufp, url_loc, &path_len);
  if (!path) {
    INKError("couldn't retrieve request path");
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  }

  query = INKUrlHttpQueryGet(bufp, url_loc, &query_len);

  if (path_len == 10 && !strncmp(path, "show-cache", 10)) {

    scan_contp = INKContCreate(cache_intercept, INKMutexCreate());
    if (INKHttpTxnIntercept(scan_contp, txnp) != INK_SUCCESS) {
      INKError("HttpTxnIntercept failed");
      INKContDestroy(scan_contp);
      goto Ldone;
    }
    cstate = (cache_scan_state *) INKmalloc(sizeof(cache_scan_state));
    memset(cstate, 0, sizeof(cache_scan_state));
    cstate->http_txnp = txnp;

    if (query && query_len > 11) {

      char querybuf[2048];
      query_len = (unsigned) query_len > sizeof(querybuf) - 1 ? sizeof(querybuf) - 1 : query_len;
      char *start = querybuf, *end = querybuf + query_len;
      size_t del_url_len;
      memcpy(querybuf, query, query_len);
      *end = '\0';
      start = strstr(querybuf, "remove_url=");
      if (start && (start == querybuf || *(start - 1) == '&')) {
        start += 11;
        if ((end = strstr(start, "&")) != NULL)
          *end = '\0';
        unescapifyStr(start);
        del_url_len = strlen(start);
        end = start + del_url_len;

        if (INKCacheKeyCreate(&cstate->key_to_delete) != INK_SUCCESS) {
          INKError("CacheKeyCreate failed");
          INKfree(cstate);
          goto Ldone;
        }

        INKDebug("cache_iter", "deleting url: %s", start);

        INKMBuffer urlBuf = INKMBufferCreate();
        INKMLoc urlLoc = INKUrlCreate(urlBuf);

        if (INKUrlParse(urlBuf, urlLoc, (const char **) &start, end) != INK_PARSE_DONE
            || INKCacheKeyDigestFromUrlSet(cstate->key_to_delete, urlLoc)
            != INK_SUCCESS) {
          INKError("CacheKeyDigestFromUrlSet failed");
          INKfree(cstate);
          INKUrlDestroy(urlBuf, urlLoc);
          INKHandleMLocRelease(urlBuf, NULL, urlLoc);
          INKCacheKeyDestroy(cstate->key_to_delete);
          goto Ldone;
        }
        INKUrlDestroy(urlBuf, urlLoc);
        INKHandleMLocRelease(urlBuf, NULL, urlLoc);
      }
    }

    if (INKContDataSet(scan_contp, cstate) != INK_SUCCESS) {
      INKError("ContDataSet failed");
      INKfree(cstate);
      goto Ldone;
    }
    INKDebug("cache_iter", "setup cache intercept");
  } else {
    INKDebug("cache_iter", "not a cache iter request");
  }

Ldone:
  INKHandleStringRelease(bufp, url_loc, path);
  INKHandleStringRelease(bufp, url_loc, query);
  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  return INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

//----------------------------------------------------------------------------
// handler for http txn events
static int
cache_print_plugin(INKCont contp, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    return setup_request(contp, (INKHttpTxn) edata);
  default:
    break;
  }
  return INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
}

//----------------------------------------------------------------------------
void
INKPluginInit(int argc, const char *argv[])
{
  global_contp = INKContCreate(cache_print_plugin, INKMutexCreate());
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, global_contp);
}
