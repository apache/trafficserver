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

/*   CacheAPITester.c
 *
 *   This plugin uses the cache APIs to cache the url requested by a client and
 *   on subsequent requests the result of a cache lookup is reported to the client
 *   by adding a header CacheTester-Result: HIT/MISS in the response to the client.
 *
 *   Note: to run negative tests DEBUG should be defined.
 *
 *
 *   Date: Sep 24, 2001
 *
 */

#include "CacheAPITester.h"
/*#define DEBUG*/



void
cache_exercise(TSHttpTxn txnp, char *url, int pin_val, int hostname_set, TSCont cache_handler_cont)
{
  TSCacheKey cache_key;
  CACHE_URL_DATA *url_data;
  int cache_ready;
  char *pchar;
  char hostname[MAX_URL_LEN];

  LOG_SET_FUNCTION_NAME("cache_exercise");

  pchar = strstr(url, "://");
  if (pchar == NULL) {
    pchar = url;
  } else {
    pchar += 3;
  }

  strncpy(hostname, pchar, MAX_URL_LEN - 1);

  pchar = strstr(hostname, "/");
  if (pchar != NULL) {
    *pchar = '\0';
  }

  if (TSCacheReady(&cache_ready) == TS_ERROR) {
    LOG_ERROR_AND_REENABLE("TSCacheReady");
    return;
  }
#ifdef DEBUG
  /*TSDebug(DEBUG_TAG, "Starting Negative Test for TSCacheReady"); */
  if (TSCacheReady(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheReady(NULL)");
  }
  /*TSDebug(DEBUG_TAG, "Done Negative Test for TSCacheReady"); */
#endif

  if (cache_ready == 0) {
    TSDebug(DEBUG_TAG, "%s: ERROR!! Cache Not Ready\n", PLUGIN_NAME);
    insert_in_response(txnp, "MISS");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  if (TSCacheKeyCreate(&cache_key) == TS_ERROR) {
    LOG_ERROR_AND_REENABLE("TSCacheKeyCreate");
    return;
  }
#ifdef DEBUG
  /*TSDebug(DEBUG_TAG, "Starting Negative Test for TSCacheKeyCreate"); */
  if (TSCacheKeyCreate(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyCreate(NULL)");
  }
  /*TSDebug(DEBUG_TAG, "Done Negative Test for TSCacheKeyCreate"); */
#endif

#ifdef DEBUG
  /*TSDebug(DEBUG_TAG, "Starting Negative Test for TSCacheKeyDigestSet"); */
  if (TSCacheKeyDigestSet(NULL, (unsigned char *) url, strlen(url)) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyDigestSet(NULL, string, len)");
  }

  if (TSCacheKeyDigestSet(cache_key, NULL, strlen(url)) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyDigestSet(cache_key, NULL, len)");
  }

  if (TSCacheKeyDigestSet(cache_key, (unsigned char *) url, -1) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyDigestSet(cache_key, string, -1)");
  }
  /*TSDebug(DEBUG_TAG, "Done Negative Test for TSCacheKeyDigestSet"); */
#endif

  if (TSCacheKeyDigestSet(cache_key, (unsigned char *) url, strlen(url)) == TS_ERROR) {
    TSCacheKeyDestroy(cache_key);
    LOG_ERROR_AND_REENABLE("TSCacheKeyDigestSet");
    return;
  }

  url_data = TSmalloc(sizeof(CACHE_URL_DATA));
  if (url_data == NULL) {
    TSCacheKeyDestroy(cache_key);
    LOG_ERROR_AND_REENABLE("TSmalloc");
    return;
  }

  url_data->magic = MAGIC_ALIVE;
  url_data->url = url;
  url_data->url_len = strlen(url);
  url_data->key = cache_key;
  url_data->pin_time = pin_val;
  url_data->write_again_after_remove = 0;
  url_data->txnp = txnp;

  url_data->bufp = TSIOBufferCreate();
  if (url_data->bufp == TS_ERROR_PTR) {
    TSCacheKeyDestroy(cache_key);
    TSfree(url_data);
    LOG_ERROR_AND_REENABLE("TSIOBufferCreate");
    return;
  }

  if (TSContDataSet(cache_handler_cont, url_data) == TS_ERROR) {
    TSCacheKeyDestroy(cache_key);
    TSfree(url_data);
    LOG_ERROR_AND_REENABLE("TSContDataSet");
    return;
  }
#ifdef DEBUG
  /*TSDebug(DEBUG_TAG, "Starting Negative Test for TSCacheKeyHostNameSet"); */
  if (TSCacheKeyHostNameSet(NULL, (unsigned char *) hostname, strlen(hostname)) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyHostNameSet(NULL, string, len)");
  }

  if (TSCacheKeyHostNameSet(url_data->key, NULL, strlen(hostname)) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyHostNameSet(cache_key, NULL, len)");
  }

  if (TSCacheKeyHostNameSet(url_data->key, (unsigned char *) hostname, -1) != TS_ERROR) {
    LOG_ERROR_NEG("TSCacheKeyHostNameSet(cache_key, string, -1)");
  }
  /*TSDebug(DEBUG_TAG, "Done Negative Test for TSCacheKeyHostNameSet"); */
#endif

  if (hostname_set > 0) {
    TSDebug(DEBUG_TAG, "HostName set for cache_key to %s", hostname);
    if (TSCacheKeyHostNameSet(url_data->key, (unsigned char *) hostname, strlen(hostname)) == TS_ERROR) {
      TSCacheKeyDestroy(cache_key);
      TSfree(url_data);
      LOG_ERROR_AND_REENABLE("TSCacheKeyHostNameSet");
      return;
    }
  }

  /* try to read from the cache */
  if (TSCacheRead(cache_handler_cont, cache_key) == TS_ERROR_PTR) {
    TSCacheKeyDestroy(cache_key);
    TSfree(url_data);
    LOG_ERROR_AND_REENABLE("TSCacheRead");
    return;
  }
#ifdef DEBUG
  /*TSDebug(DEBUG_TAG, "Starting Negative Test for TSCacheRead"); */
  if (TSCacheRead(cache_handler_cont, NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSCacheRead(cache_handler_cont, NULL)");
  }

  if (TSCacheRead(NULL, cache_key) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSCacheRead(NULL, cache_key)");
  }
  /*TSDebug(DEBUG_TAG, "Done Negative Test for TSCacheRead"); */
#endif

  return;
}

static int
handle_cache_events(TSCont contp, TSEvent event, void *edata)
{
  CACHE_URL_DATA *url_data;
  TSVConn connp = (TSVConn) edata;
  char tempstr[32];

  LOG_SET_FUNCTION_NAME("handle_cache_events");

  url_data = (CACHE_URL_DATA *) TSContDataGet(contp);
  if (url_data == TS_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("TSContDataGet");
  }

  if (event != TS_EVENT_HTTP_TXN_CLOSE) {
    TSReleaseAssert(url_data->magic == MAGIC_ALIVE);
  } else {
    TSReleaseAssert((url_data == NULL) || (url_data->magic == MAGIC_ALIVE));
  }

  switch (event) {
  case TS_EVENT_CACHE_OPEN_READ:
    /*handle_cache_read(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_OPEN_READ\n");

    if (url_data->pin_time != 0) {
      sprintf(tempstr, "PIN%d", url_data->pin_time);
    } else {
      sprintf(tempstr, "HIT");
    }
    insert_in_response(url_data->txnp, tempstr);

    if (url_data->pin_time != 0) {

      url_data->write_again_after_remove = 1;

      if (TSCacheRemove(contp, url_data->key) == TS_ERROR_PTR) {
        LOG_ERROR("TSCacheRemove");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (TSCacheRemove(NULL, url_data->key) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSCacheRemove(NULL, cache_key)");
      }

      if (TSCacheRemove(contp, NULL) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSCacheRemove(contp, NULL)");
      }
#endif
      return 0;
    }
#ifdef DEBUG
    if (TSVConnRead(NULL, contp, url_data->bufp, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnRead(NULL, contp, bufp, url_len)");
    }

    if (TSVConnRead(connp, NULL, url_data->bufp, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnRead(connp, NULL, bufp, url_len)");
    }

    if (TSVConnRead(connp, contp, NULL, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnRead(connp, contp, NULL, url_len)");
    }

    if (TSVConnRead(connp, contp, url_data->bufp, -1) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnRead(connp, contp, bufp, -1)");
    }
#endif

    if (TSVConnRead(connp, contp, url_data->bufp, url_data->url_len) == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnRead");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    break;

  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    /*handle_cache_read_fail(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_OPEN_READ_FAILED(%d)\n", edata);

    if (url_data->pin_time != 0) {
      sprintf(tempstr, "PIN%d", url_data->pin_time);
    } else {
      sprintf(tempstr, "MISS");
    }
    insert_in_response(url_data->txnp, tempstr);

    if (url_data->pin_time != 0) {
      TSDebug(DEBUG_TAG, "url Pinned in cache for %d secs", url_data->pin_time);
      if (TSCacheKeyPinnedSet(url_data->key, url_data->pin_time) == TS_ERROR) {
        LOG_ERROR("TSCacheKeyPinnedSet");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (TSCacheKeyPinnedSet(NULL, url_data->pin_time) != TS_ERROR) {
        LOG_ERROR_NEG("TSCacheKeyPinnedSet(NULL, pin_time)");
      }

      if (TSCacheKeyPinnedSet(url_data->key, -1) != TS_ERROR) {
        LOG_ERROR_NEG("TSCacheKeyPinnedSet(cache_key, -1)");
      }
#endif
    }

    if (TSCacheWrite(contp, url_data->key) == TS_ERROR_PTR) {
      LOG_ERROR("TSCacheWrite");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (TSCacheWrite(contp, NULL) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSCacheWrite(contp, NULL)");
    }

    if (TSCacheWrite(NULL, url_data->key) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSCacheWrite(NULL, url_data->key)");
    }
#endif

    break;

  case TS_EVENT_CACHE_OPEN_WRITE:
    /*handle_cache_write(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_OPEN_WRITE\n");

    if (TSIOBufferWrite(url_data->bufp, url_data->url, url_data->url_len) == TS_ERROR) {
      LOG_ERROR("TSIOBufferWrite");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    url_data->bufp_reader = TSIOBufferReaderAlloc(url_data->bufp);
    if (url_data->bufp_reader == TS_ERROR_PTR) {
      LOG_ERROR("TSIOBufferReaderAlloc");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (TSVConnWrite(NULL, contp, url_data->bufp_reader, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnWrite(NULL, contp, bufp_reader, url_len");
    }

    if (TSVConnWrite(connp, NULL, url_data->bufp_reader, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnWrite(connp, NULL, bufp_reader, url_len");
    }

    if (TSVConnWrite(connp, contp, NULL, url_data->url_len) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnWrite(connp, contp, NULL, url_len");
    }

    if (TSVConnWrite(connp, contp, url_data->bufp_reader, -1) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVConnWrite(connp, contp, bufp_reader, -1");
    }
#endif

    if (TSVConnWrite(connp, contp, url_data->bufp_reader, url_data->url_len) == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnWrite");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    break;

  case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
    /*handle_cache_write_fail(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_OPEN_WRITE_FAILED(%d)\n", edata);
    TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);

    break;

  case TS_EVENT_CACHE_REMOVE:
    /*handle_cache_remove(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_REMOVE\n");

    if (url_data->write_again_after_remove != 0) {

      TSDebug(DEBUG_TAG, "url Pinned in cache for %d secs", url_data->pin_time);
      if (url_data->pin_time != 0) {
        if (TSCacheKeyPinnedSet(url_data->key, url_data->pin_time) == TS_ERROR) {
          LOG_ERROR("TSCacheKeyPinnedSet");
          TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
          return -1;
        }
      }

      if (TSCacheWrite(contp, url_data->key) == TS_ERROR_PTR) {
        LOG_ERROR("TSCacheWrite");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }
    } else {
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
    }

    break;

  case TS_EVENT_CACHE_REMOVE_FAILED:
    /*handle_cache_remove_fail(); */
    TSDebug(DEBUG_TAG, "TS_EVENT_CACHE_REMOVE_FAILED(%d)\n", edata);
    TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_VCONN_READ_READY:
    TSDebug(DEBUG_TAG, "TS_EVENT_VCONN_READ_READY\n");

    if (TSVIOReenable(edata) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
    }
#ifdef DEBUG
    if (TSVIOReenable(NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSVIOReenable");
    }
#endif

    break;

  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(DEBUG_TAG, "TS_EVENT_VCONN_WRITE_READY\n");

    if (TSVIOReenable(edata) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
    }
#ifdef DEBUG
    if (TSVIOReenable(NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSVIOReenable");
    }
#endif

    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
    TSDebug(DEBUG_TAG, "TS_EVENT_VCONN_READ_COMPLETE\n");
    {
      TSIOBufferBlock blk;
      char *src;
      char dst[MAX_URL_LEN];
      int avail;
      int url_len_from_cache;

      if ((connp = TSVIOVConnGet(edata)) == TS_ERROR_PTR) {
        LOG_ERROR("TSVIOVConnGet");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (TSVConnCacheObjectSizeGet(NULL, &url_len_from_cache) != TS_ERROR) {
        LOG_ERROR_NEG("TSVConnCacheObjectSizeGet(NULL, &size)");
      }

      if (TSVConnCacheObjectSizeGet(connp, NULL) != TS_ERROR) {
        LOG_ERROR_NEG("TSVConnCacheObjectSizeGet(inkvconn, NULL)");
      }
#endif

      if (TSVConnCacheObjectSizeGet(connp, &url_len_from_cache) == TS_ERROR) {
        LOG_ERROR("TSVConnCacheObjectSizeGet");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }

      if (url_len_from_cache != url_data->url_len) {
        LOG_ERROR("TSVConnCacheObjectSizeGet-mismatch");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }

      if ((connp = TSVIOVConnGet(edata)) == TS_ERROR_PTR) {
        LOG_ERROR("TSVIOVConnGet");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (TSVIOVConnGet(NULL) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSVIOVConnGet(null)");
      }

      if (TSVConnClose(NULL) != TS_ERROR) {
        LOG_ERROR_NEG("TSVConnClose(NULL)");
      }
#endif

      if (TSVConnClose(connp) == TS_ERROR) {
        LOG_ERROR("TSVConnClose");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }

      url_data = (CACHE_URL_DATA *) TSContDataGet(contp);
      TSReleaseAssert(url_data->magic == MAGIC_ALIVE);

      url_data->bufp_reader = TSIOBufferReaderAlloc(url_data->bufp);
      blk = TSIOBufferReaderStart(url_data->bufp_reader);
      if (blk == TS_ERROR_PTR) {
        LOG_ERROR("TSIOBufferReaderStart");
        TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
        return -1;
      }

      src = (char *) TSIOBufferBlockReadStart(blk, url_data->bufp_reader, &avail);
      /* FC: make sure we do not copy more than MAX_URL_LEN-1 bytes into dst */
      if (avail < 0) {
        avail = 0;
      }
      avail = (avail < MAX_URL_LEN - 1) ? avail : (MAX_URL_LEN - 1);
      strncpy(dst, src, avail);
      dst[avail] = '\0';

      if (strcmp(dst, url_data->url) != 0) {
        TSDebug(DEBUG_TAG, "URL in cache NO_MATCH\ndst=[%s]\nurl=[%s]\n", dst, url_data->url);
      }
    }
    TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);

    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(DEBUG_TAG, "TS_EVENT_VCONN_WRITE_COMPLETE\n");

    if ((connp = TSVIOVConnGet(edata)) == TS_ERROR_PTR) {
      LOG_ERROR("TSVIOVConnGet");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (TSVIOVConnGet(NULL) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSVIOVConnGet(null)");
    }

    if (TSVConnClose(NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSVConnClose(NULL)");
    }
#endif

    if (TSVConnClose(connp) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
      TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_VCONN_EOS:
    TSDebug(DEBUG_TAG, "TS_EVENT_VCONN_EOS\n");
    break;

  case TS_EVENT_ERROR:
    TSDebug(DEBUG_TAG, "TS_EVENT_ERROR\n");
    TSHttpTxnReenable(url_data->txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(DEBUG_TAG, "TS_EVENT_HTTP_TXN_CLOSE\n");

    if (url_data == NULL) {
      TSHttpTxnReenable(edata, TS_EVENT_HTTP_CONTINUE);
      break;
    }

    if (url_data->url != NULL) {
      TSfree(url_data->url);
    }

    if (TSCacheKeyDestroy(url_data->key) == TS_ERROR) {
      LOG_ERROR("TSCacheKeyDestroy");
    }
#ifdef DEBUG
    if (TSCacheKeyDestroy(NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSCacheKeyDestroy(NULL)");
    }

    if (TSIOBufferDestroy(NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSIOBufferDestroy(NULL)");
    }
#endif

    if (TSIOBufferDestroy(url_data->bufp) == TS_ERROR) {
      LOG_ERROR("TSIOBufferDestroy");
    }

    url_data->magic = MAGIC_DEAD;
    TSfree(url_data);
    TSContDestroy(contp);
    TSHttpTxnReenable(edata, TS_EVENT_HTTP_CONTINUE);

    break;

  default:
    ;
  }
  return 0;
}

static int
event_mux(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSCont newcont;
  char *client_req;
  char *url_line;
  int ret_status;
  int neg_test_val;
  int pin_val;

  LOG_SET_FUNCTION_NAME("event_mux");

  switch (event) {

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    if ((newcont = TSContCreate(handle_cache_events, TSMutexCreate())) == TS_ERROR_PTR) {
      LOG_ERROR_AND_REENABLE("TSContCreate");
      return 0;
    }

    /* FC When called back for txn close, the plugins test if the value of the
       continuation data ptr is null or not. Initialize it here to null. */
    TSContDataSet(newcont, NULL);

    if (TSHttpTxnHookAdd(edata, TS_HTTP_TXN_CLOSE_HOOK, newcont) == TS_ERROR) {
      LOG_ERROR_AND_REENABLE("TSHttpTxnHookAdd");
      return 0;
    }

    ret_status = get_client_req(txnp, &url_line, &client_req, &neg_test_val, &pin_val);

    /* FC: added test on url and client req too */
    if ((ret_status == -1) || (url_line == NULL) || (client_req == NULL)) {
      TSDebug(DEBUG_TAG, "Unable to get client request header\n");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }

    TSDebug(DEBUG_TAG, "\n%s\n%s", url_line, client_req);
    TSfree(client_req);

    cache_exercise(txnp, url_line, pin_val, neg_test_val, newcont);

    break;

  default:
    ;
  }
  return 0;
}

int
insert_in_response(TSHttpTxn txnp, char *result_val)
{
  TSMBuffer resp_bufp;
  TSMLoc resp_loc;
  TSMLoc field_loc;

  LOG_SET_FUNCTION_NAME("insert_in_response");

#ifdef DEBUG
  if (TSHttpTxnClientRespGet(NULL, &resp_bufp, &resp_loc) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientRespGet(null, buf, hdr_loc)");
  }

  if (TSHttpTxnClientRespGet(txnp, NULL, &resp_loc) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientRespGet(txnp, null, hdr_loc)");
  }

  if (TSHttpTxnClientRespGet(txnp, &resp_bufp, NULL) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientRespGet(txnp, buf, null)");
  }
#endif

  if (!TSHttpTxnClientRespGet(txnp, &resp_bufp, &resp_loc)) {
    LOG_ERROR_AND_RETURN("TSHttpTxnClientRespGet");
  }

  /* create a new field in the response header */
  if ((field_loc = TSMimeHdrFieldCreate(resp_bufp, resp_loc)) == TS_ERROR_PTR) {
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldCreate");
  }

  /* set its name */
  if (TSMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, "CacheTester-Result", 18) == TS_ERROR) {
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldNameSet");
  }

  /* set its value */
  if (TSMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, field_loc, -1, result_val, strlen(result_val)) == TS_ERROR) {
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldValueIntInsert");
  }

  /* insert it into the header */
  if (TSMimeHdrFieldAppend(resp_bufp, resp_loc, field_loc) == TS_ERROR) {
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldAppend");
  }

  if (TSHandleMLocRelease(resp_bufp, resp_loc, field_loc) == TS_ERROR) {
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
  }

  if (TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc) == TS_ERROR) {
    LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
  }

  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");
  TSCont contp;

  TSDebug(DEBUG_TAG, "TSPluginInit");
  if ((contp = TSContCreate(event_mux, NULL)) == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
  } else if (TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp) == TS_ERROR) {
    LOG_ERROR("TSHttpHookAdd");
  }

}


/* pointers returned by p_url_line and p_client_req should be freed by TSfree() */

int
get_client_req(TSHttpTxn txnp, char **p_url_line, char **p_client_req, int *neg_test_val, int *pin_val)
{
  TSMBuffer bufp;
  TSReturnCode ret_code;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  TSMLoc neg_loc;
  TSMLoc pin_loc;

  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  int total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string;
  char *url_str;
  int url_len;
  int output_len;

  LOG_SET_FUNCTION_NAME("get_client_req");

  *p_url_line = NULL;
  *p_client_req = NULL;
  *neg_test_val = 0;
  *pin_val = 0;

#ifdef DEBUG
  if (TSHttpTxnClientReqGet(NULL, &bufp, &hdr_loc) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientReqGet(null, buf, hdr_loc)");
  }

  if (TSHttpTxnClientReqGet(txnp, NULL, &hdr_loc) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientReqGet(txnp, null, hdr_loc)");
  }

  if (TSHttpTxnClientReqGet(txnp, &bufp, NULL) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientReqGet(txnp, buf, null)");
  }
#endif

  if (!TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    LOG_ERROR_AND_RETURN("TSHttpTxnClientReqGet");
  }

  if ((url_loc = TSHttpHdrUrlGet(bufp, hdr_loc)) == TS_ERROR_PTR) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("TSHttpHdrUrlGet");
  }
#ifdef DEBUG
  if (TSUrlStringGet(NULL, url_loc, &url_len) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSUrlStringGet(NULL, url_loc, &int)");
  }

  if (TSUrlStringGet(bufp, NULL, &url_len) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSUrlStringGet(bufp, NULL, &int)");
  }
#endif

  if ((url_str = TSUrlStringGet(bufp, url_loc, &url_len)) == TS_ERROR_PTR) {
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("TSUrlStringGet");
  }

  if (TSHandleMLocRelease(bufp, hdr_loc, url_loc) == TS_ERROR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
  }

  url_str[url_len] = '\0';
  *p_url_line = url_str;

  if ((output_buffer = TSIOBufferCreate()) == TS_ERROR_PTR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("TSIOBufferCreate");
  }
#ifdef DEBUG
  if (TSIOBufferReaderAlloc(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferReaderAlloc(NULL)");
  }
#endif

  if ((reader = TSIOBufferReaderAlloc(output_buffer)) == TS_ERROR_PTR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSIOBufferReaderAlloc");
  }

  /* This will print  just MIMEFields and not
     the http request line */
  if (TSMimeHdrPrint(bufp, hdr_loc, output_buffer) == TS_ERROR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSMimeHdrPrint");
  }

  if ((neg_loc = TSMimeHdrFieldFind(bufp, hdr_loc, "CacheTester-HostNameSet", -1)) == TS_ERROR_PTR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldFind");
  }

  if (neg_loc != NULL) {

    ret_code = TSMimeHdrFieldValueIntGet(bufp, hdr_loc, neg_loc, 0, neg_test_val);
    if (ret_code == TS_ERROR) {
      TSfree(url_str);
      TSHandleMLocRelease(bufp, hdr_loc, neg_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("TSMimeHdrFieldValueIntGet");
    }

    if (TSHandleMLocRelease(bufp, hdr_loc, neg_loc) == TS_ERROR) {
      TSfree(url_str);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
    }
  }

  if ((pin_loc = TSMimeHdrFieldFind(bufp, hdr_loc, "CacheTester-Pin", -1)) == TS_ERROR_PTR) {
    TSfree(url_str);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSMimeHdrFieldFind");
  }

  if (pin_loc != NULL) {

    ret_code = TSMimeHdrFieldValueIntGet(bufp, hdr_loc, pin_loc, 0, pin_val);
    if (ret_code == TS_ERROR) {
      TSfree(url_str);
      TSHandleMLocRelease(bufp, hdr_loc, pin_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("TSMimeHdrFieldValueIntGet");
    }

    if (TSHandleMLocRelease(bufp, hdr_loc, pin_loc) == TS_ERROR) {
      TSfree(url_str);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
    }
  }

  if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) == TS_ERROR) {
    TSfree(url_str);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSHandleMLocRelease");
  }

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  if ((total_avail = TSIOBufferReaderAvail(reader)) == TS_ERROR) {
    TSfree(url_str);
    TSIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("TSIOBufferReaderAvail");
  }
#ifdef DEBUG
  if (TSIOBufferReaderAvail(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferReaderAvail(NULL)");
  }
#endif

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) TSmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = TSIOBufferReaderStart(reader);

  if (block == TS_ERROR_PTR) {
    TSfree(url_str);
    TSfree(output_string);
    LOG_ERROR_AND_RETURN("TSIOBufferReaderStart");
  }
#ifdef DEBUG
  if (TSIOBufferReaderStart(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferReaderStart(NULL)");
  }

  if (TSIOBufferBlockReadStart(NULL, reader, &block_avail) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferBlockReadStart(null, reader, &int)");
  }

  if (TSIOBufferBlockReadStart(block, NULL, &block_avail) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferBlockReadStart(block, null, &int)");
  }
#endif

  while (block) {

    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);

    if (block_start == TS_ERROR_PTR) {
      TSfree(url_str);
      TSfree(output_string);
      LOG_ERROR_AND_RETURN("TSIOBufferBlockReadStart");
    }

    /* FC paranoia: make sure we don't copy more bytes than buffer size can handle */
    if ((output_len + block_avail) > total_avail) {
      TSfree(url_str);
      TSfree(output_string);
      LOG_ERROR_AND_RETURN("More bytes than expected in IOBuffer");
    }

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    if (TSIOBufferReaderConsume(reader, block_avail) == TS_ERROR) {
      TSfree(url_str);
      TSfree(output_string);
      LOG_ERROR_AND_RETURN("TSIOBufferReaderConsume");
    }
#ifdef DEBUG
    if (TSIOBufferReaderConsume(NULL, block_avail) != TS_ERROR) {
      LOG_ERROR_NEG("TSIOBufferReaderConsume(null, int)");
    }

    if (TSIOBufferReaderConsume(reader, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSIOBufferReaderConsume(reader, -1)");
    }
#endif

    /* Get the next block now that we've consumed the
       data off the last block */
    if ((block = TSIOBufferReaderStart(reader)) == TS_ERROR_PTR) {
      TSfree(url_str);
      TSfree(output_string);
      LOG_ERROR_AND_RETURN("TSIOBufferReaderStart");
    }
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  /*output_len++; */

  /* Free up the TSIOBuffer that we used to print out the header */
  if (TSIOBufferReaderFree(reader) == TS_ERROR) {
    TSfree(url_str);
    TSfree(output_string);
    LOG_ERROR_AND_RETURN("TSIOBufferReaderFree");
  }
#ifdef DEBUG
  if (TSIOBufferReaderFree(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferReaderFree(NULL)");
  }
#endif

  if (TSIOBufferDestroy(output_buffer) == TS_ERROR) {
    TSfree(url_str);
    TSfree(output_string);
    LOG_ERROR_AND_RETURN("TSIOBufferDestroy");
  }

  *p_client_req = output_string;
  return 1;
}
