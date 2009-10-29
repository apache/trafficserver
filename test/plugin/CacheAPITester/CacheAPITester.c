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
cache_exercise(INKHttpTxn txnp, char *url, int pin_val, int hostname_set, INKCont cache_handler_cont)
{
  INKCacheKey cache_key;
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

  if (INKCacheReady(&cache_ready) == INK_ERROR) {
    LOG_ERROR_AND_REENABLE("INKCacheReady");
    return;
  }
#ifdef DEBUG
  /*INKDebug(DEBUG_TAG, "Starting Negative Test for INKCacheReady"); */
  if (INKCacheReady(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheReady(NULL)");
  }
  /*INKDebug(DEBUG_TAG, "Done Negative Test for INKCacheReady"); */
#endif

  if (cache_ready == 0) {
    INKDebug(DEBUG_TAG, "%s: ERROR!! Cache Not Ready\n", PLUGIN_NAME);
    insert_in_response(txnp, "MISS");
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return;
  }

  if (INKCacheKeyCreate(&cache_key) == INK_ERROR) {
    LOG_ERROR_AND_REENABLE("INKCacheKeyCreate");
    return;
  }
#ifdef DEBUG
  /*INKDebug(DEBUG_TAG, "Starting Negative Test for INKCacheKeyCreate"); */
  if (INKCacheKeyCreate(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyCreate(NULL)");
  }
  /*INKDebug(DEBUG_TAG, "Done Negative Test for INKCacheKeyCreate"); */
#endif

#ifdef DEBUG
  /*INKDebug(DEBUG_TAG, "Starting Negative Test for INKCacheKeyDigestSet"); */
  if (INKCacheKeyDigestSet(NULL, (unsigned char *) url, strlen(url)) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyDigestSet(NULL, string, len)");
  }

  if (INKCacheKeyDigestSet(cache_key, NULL, strlen(url)) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyDigestSet(cache_key, NULL, len)");
  }

  if (INKCacheKeyDigestSet(cache_key, (unsigned char *) url, -1) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyDigestSet(cache_key, string, -1)");
  }
  /*INKDebug(DEBUG_TAG, "Done Negative Test for INKCacheKeyDigestSet"); */
#endif

  if (INKCacheKeyDigestSet(cache_key, (unsigned char *) url, strlen(url)) == INK_ERROR) {
    INKCacheKeyDestroy(cache_key);
    LOG_ERROR_AND_REENABLE("INKCacheKeyDigestSet");
    return;
  }

  url_data = INKmalloc(sizeof(CACHE_URL_DATA));
  if (url_data == NULL) {
    INKCacheKeyDestroy(cache_key);
    LOG_ERROR_AND_REENABLE("INKmalloc");
    return;
  }

  url_data->magic = MAGIC_ALIVE;
  url_data->url = url;
  url_data->url_len = strlen(url);
  url_data->key = cache_key;
  url_data->pin_time = pin_val;
  url_data->write_again_after_remove = 0;
  url_data->txnp = txnp;

  url_data->bufp = INKIOBufferCreate();
  if (url_data->bufp == INK_ERROR_PTR) {
    INKCacheKeyDestroy(cache_key);
    INKfree(url_data);
    LOG_ERROR_AND_REENABLE("INKIOBufferCreate");
    return;
  }

  if (INKContDataSet(cache_handler_cont, url_data) == INK_ERROR) {
    INKCacheKeyDestroy(cache_key);
    INKfree(url_data);
    LOG_ERROR_AND_REENABLE("INKContDataSet");
    return;
  }
#ifdef DEBUG
  /*INKDebug(DEBUG_TAG, "Starting Negative Test for INKCacheKeyHostNameSet"); */
  if (INKCacheKeyHostNameSet(NULL, (unsigned char *) hostname, strlen(hostname)) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyHostNameSet(NULL, string, len)");
  }

  if (INKCacheKeyHostNameSet(url_data->key, NULL, strlen(hostname)) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyHostNameSet(cache_key, NULL, len)");
  }

  if (INKCacheKeyHostNameSet(url_data->key, (unsigned char *) hostname, -1) != INK_ERROR) {
    LOG_ERROR_NEG("INKCacheKeyHostNameSet(cache_key, string, -1)");
  }
  /*INKDebug(DEBUG_TAG, "Done Negative Test for INKCacheKeyHostNameSet"); */
#endif

  if (hostname_set > 0) {
    INKDebug(DEBUG_TAG, "HostName set for cache_key to %s", hostname);
    if (INKCacheKeyHostNameSet(url_data->key, (unsigned char *) hostname, strlen(hostname)) == INK_ERROR) {
      INKCacheKeyDestroy(cache_key);
      INKfree(url_data);
      LOG_ERROR_AND_REENABLE("INKCacheKeyHostNameSet");
      return;
    }
  }

  /* try to read from the cache */
  if (INKCacheRead(cache_handler_cont, cache_key) == INK_ERROR_PTR) {
    INKCacheKeyDestroy(cache_key);
    INKfree(url_data);
    LOG_ERROR_AND_REENABLE("INKCacheRead");
    return;
  }
#ifdef DEBUG
  /*INKDebug(DEBUG_TAG, "Starting Negative Test for INKCacheRead"); */
  if (INKCacheRead(cache_handler_cont, NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKCacheRead(cache_handler_cont, NULL)");
  }

  if (INKCacheRead(NULL, cache_key) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKCacheRead(NULL, cache_key)");
  }
  /*INKDebug(DEBUG_TAG, "Done Negative Test for INKCacheRead"); */
#endif

  return;
}

static int
handle_cache_events(INKCont contp, INKEvent event, void *edata)
{
  CACHE_URL_DATA *url_data;
  INKVConn connp = (INKVConn) edata;
  char tempstr[32];

  LOG_SET_FUNCTION_NAME("handle_cache_events");

  url_data = (CACHE_URL_DATA *) INKContDataGet(contp);
  if (url_data == INK_ERROR_PTR) {
    LOG_ERROR_AND_RETURN("INKContDataGet");
  }

  if (event != INK_EVENT_HTTP_TXN_CLOSE) {
    INKReleaseAssert(url_data->magic == MAGIC_ALIVE);
  } else {
    INKReleaseAssert((url_data == NULL) || (url_data->magic == MAGIC_ALIVE));
  }

  switch (event) {
  case INK_EVENT_CACHE_OPEN_READ:
    /*handle_cache_read(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_OPEN_READ\n");

    if (url_data->pin_time != 0) {
      sprintf(tempstr, "PIN%d", url_data->pin_time);
    } else {
      sprintf(tempstr, "HIT");
    }
    insert_in_response(url_data->txnp, tempstr);

    if (url_data->pin_time != 0) {

      url_data->write_again_after_remove = 1;

      if (INKCacheRemove(contp, url_data->key) == INK_ERROR_PTR) {
        LOG_ERROR("INKCacheRemove");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (INKCacheRemove(NULL, url_data->key) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKCacheRemove(NULL, cache_key)");
      }

      if (INKCacheRemove(contp, NULL) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKCacheRemove(contp, NULL)");
      }
#endif
      return 0;
    }
#ifdef DEBUG
    if (INKVConnRead(NULL, contp, url_data->bufp, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnRead(NULL, contp, bufp, url_len)");
    }

    if (INKVConnRead(connp, NULL, url_data->bufp, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnRead(connp, NULL, bufp, url_len)");
    }

    if (INKVConnRead(connp, contp, NULL, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnRead(connp, contp, NULL, url_len)");
    }

    if (INKVConnRead(connp, contp, url_data->bufp, -1) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnRead(connp, contp, bufp, -1)");
    }
#endif

    if (INKVConnRead(connp, contp, url_data->bufp, url_data->url_len) == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnRead");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }

    break;

  case INK_EVENT_CACHE_OPEN_READ_FAILED:
    /*handle_cache_read_fail(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_OPEN_READ_FAILED(%d)\n", edata);

    if (url_data->pin_time != 0) {
      sprintf(tempstr, "PIN%d", url_data->pin_time);
    } else {
      sprintf(tempstr, "MISS");
    }
    insert_in_response(url_data->txnp, tempstr);

    if (url_data->pin_time != 0) {
      INKDebug(DEBUG_TAG, "url Pinned in cache for %d secs", url_data->pin_time);
      if (INKCacheKeyPinnedSet(url_data->key, url_data->pin_time) == INK_ERROR) {
        LOG_ERROR("INKCacheKeyPinnedSet");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (INKCacheKeyPinnedSet(NULL, url_data->pin_time) != INK_ERROR) {
        LOG_ERROR_NEG("INKCacheKeyPinnedSet(NULL, pin_time)");
      }

      if (INKCacheKeyPinnedSet(url_data->key, -1) != INK_ERROR) {
        LOG_ERROR_NEG("INKCacheKeyPinnedSet(cache_key, -1)");
      }
#endif
    }

    if (INKCacheWrite(contp, url_data->key) == INK_ERROR_PTR) {
      LOG_ERROR("INKCacheWrite");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (INKCacheWrite(contp, NULL) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKCacheWrite(contp, NULL)");
    }

    if (INKCacheWrite(NULL, url_data->key) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKCacheWrite(NULL, url_data->key)");
    }
#endif

    break;

  case INK_EVENT_CACHE_OPEN_WRITE:
    /*handle_cache_write(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_OPEN_WRITE\n");

    if (INKIOBufferWrite(url_data->bufp, url_data->url, url_data->url_len) == INK_ERROR) {
      LOG_ERROR("INKIOBufferWrite");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }

    url_data->bufp_reader = INKIOBufferReaderAlloc(url_data->bufp);
    if (url_data->bufp_reader == INK_ERROR_PTR) {
      LOG_ERROR("INKIOBufferReaderAlloc");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (INKVConnWrite(NULL, contp, url_data->bufp_reader, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnWrite(NULL, contp, bufp_reader, url_len");
    }

    if (INKVConnWrite(connp, NULL, url_data->bufp_reader, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnWrite(connp, NULL, bufp_reader, url_len");
    }

    if (INKVConnWrite(connp, contp, NULL, url_data->url_len) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnWrite(connp, contp, NULL, url_len");
    }

    if (INKVConnWrite(connp, contp, url_data->bufp_reader, -1) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVConnWrite(connp, contp, bufp_reader, -1");
    }
#endif

    if (INKVConnWrite(connp, contp, url_data->bufp_reader, url_data->url_len) == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnWrite");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }

    break;

  case INK_EVENT_CACHE_OPEN_WRITE_FAILED:
    /*handle_cache_write_fail(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_OPEN_WRITE_FAILED(%d)\n", edata);
    INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);

    break;

  case INK_EVENT_CACHE_REMOVE:
    /*handle_cache_remove(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_REMOVE\n");

    if (url_data->write_again_after_remove != 0) {

      INKDebug(DEBUG_TAG, "url Pinned in cache for %d secs", url_data->pin_time);
      if (url_data->pin_time != 0) {
        if (INKCacheKeyPinnedSet(url_data->key, url_data->pin_time) == INK_ERROR) {
          LOG_ERROR("INKCacheKeyPinnedSet");
          INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
          return -1;
        }
      }

      if (INKCacheWrite(contp, url_data->key) == INK_ERROR_PTR) {
        LOG_ERROR("INKCacheWrite");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }
    } else {
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
    }

    break;

  case INK_EVENT_CACHE_REMOVE_FAILED:
    /*handle_cache_remove_fail(); */
    INKDebug(DEBUG_TAG, "INK_EVENT_CACHE_REMOVE_FAILED(%d)\n", edata);
    INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_VCONN_READ_READY:
    INKDebug(DEBUG_TAG, "INK_EVENT_VCONN_READ_READY\n");

    if (INKVIOReenable(edata) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
    }
#ifdef DEBUG
    if (INKVIOReenable(NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKVIOReenable");
    }
#endif

    break;

  case INK_EVENT_VCONN_WRITE_READY:
    INKDebug(DEBUG_TAG, "INK_EVENT_VCONN_WRITE_READY\n");

    if (INKVIOReenable(edata) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
    }
#ifdef DEBUG
    if (INKVIOReenable(NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKVIOReenable");
    }
#endif

    break;

  case INK_EVENT_VCONN_READ_COMPLETE:
    INKDebug(DEBUG_TAG, "INK_EVENT_VCONN_READ_COMPLETE\n");
    {
      INKIOBufferBlock blk;
      char *src;
      char dst[MAX_URL_LEN];
      int avail;
      int url_len_from_cache;

      if ((connp = INKVIOVConnGet(edata)) == INK_ERROR_PTR) {
        LOG_ERROR("INKVIOVConnGet");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (INKVConnCacheObjectSizeGet(NULL, &url_len_from_cache) != INK_ERROR) {
        LOG_ERROR_NEG("INKVConnCacheObjectSizeGet(NULL, &size)");
      }

      if (INKVConnCacheObjectSizeGet(connp, NULL) != INK_ERROR) {
        LOG_ERROR_NEG("INKVConnCacheObjectSizeGet(inkvconn, NULL)");
      }
#endif

      if (INKVConnCacheObjectSizeGet(connp, &url_len_from_cache) == INK_ERROR) {
        LOG_ERROR("INKVConnCacheObjectSizeGet");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }

      if (url_len_from_cache != url_data->url_len) {
        LOG_ERROR("INKVConnCacheObjectSizeGet-mismatch");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }

      if ((connp = INKVIOVConnGet(edata)) == INK_ERROR_PTR) {
        LOG_ERROR("INKVIOVConnGet");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }
#ifdef DEBUG
      if (INKVIOVConnGet(NULL) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKVIOVConnGet(null)");
      }

      if (INKVConnClose(NULL) != INK_ERROR) {
        LOG_ERROR_NEG("INKVConnClose(NULL)");
      }
#endif

      if (INKVConnClose(connp) == INK_ERROR) {
        LOG_ERROR("INKVConnClose");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }

      url_data = (CACHE_URL_DATA *) INKContDataGet(contp);
      INKReleaseAssert(url_data->magic == MAGIC_ALIVE);

      url_data->bufp_reader = INKIOBufferReaderAlloc(url_data->bufp);
      blk = INKIOBufferReaderStart(url_data->bufp_reader);
      if (blk == INK_ERROR_PTR) {
        LOG_ERROR("INKIOBufferReaderStart");
        INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
        return -1;
      }

      src = (char *) INKIOBufferBlockReadStart(blk, url_data->bufp_reader, &avail);
      /* FC: make sure we do not copy more than MAX_URL_LEN-1 bytes into dst */
      if (avail < 0) {
        avail = 0;
      }
      avail = (avail < MAX_URL_LEN - 1) ? avail : (MAX_URL_LEN - 1);
      strncpy(dst, src, avail);
      dst[avail] = '\0';

      if (strcmp(dst, url_data->url) != 0) {
        INKDebug(DEBUG_TAG, "URL in cache NO_MATCH\ndst=[%s]\nurl=[%s]\n", dst, url_data->url);
      }
    }
    INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);

    break;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug(DEBUG_TAG, "INK_EVENT_VCONN_WRITE_COMPLETE\n");

    if ((connp = INKVIOVConnGet(edata)) == INK_ERROR_PTR) {
      LOG_ERROR("INKVIOVConnGet");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }
#ifdef DEBUG
    if (INKVIOVConnGet(NULL) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKVIOVConnGet(null)");
    }

    if (INKVConnClose(NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKVConnClose(NULL)");
    }
#endif

    if (INKVConnClose(connp) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
      INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
      return -1;
    }

    INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_VCONN_EOS:
    INKDebug(DEBUG_TAG, "INK_EVENT_VCONN_EOS\n");
    break;

  case INK_EVENT_ERROR:
    INKDebug(DEBUG_TAG, "INK_EVENT_ERROR\n");
    INKHttpTxnReenable(url_data->txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    INKDebug(DEBUG_TAG, "INK_EVENT_HTTP_TXN_CLOSE\n");

    if (url_data == NULL) {
      INKHttpTxnReenable(edata, INK_EVENT_HTTP_CONTINUE);
      break;
    }

    if (url_data->url != NULL) {
      INKfree(url_data->url);
    }

    if (INKCacheKeyDestroy(url_data->key) == INK_ERROR) {
      LOG_ERROR("INKCacheKeyDestroy");
    }
#ifdef DEBUG
    if (INKCacheKeyDestroy(NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKCacheKeyDestroy(NULL)");
    }

    if (INKIOBufferDestroy(NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKIOBufferDestroy(NULL)");
    }
#endif

    if (INKIOBufferDestroy(url_data->bufp) == INK_ERROR) {
      LOG_ERROR("INKIOBufferDestroy");
    }

    url_data->magic = MAGIC_DEAD;
    INKfree(url_data);
    INKContDestroy(contp);
    INKHttpTxnReenable(edata, INK_EVENT_HTTP_CONTINUE);

    break;

  default:
    ;
  }
  return 0;
}

static int
event_mux(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKCont newcont;
  char *client_req;
  char *url_line;
  int ret_status;
  int neg_test_val;
  int pin_val;

  LOG_SET_FUNCTION_NAME("event_mux");

  switch (event) {

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    if ((newcont = INKContCreate(handle_cache_events, INKMutexCreate())) == INK_ERROR_PTR) {
      LOG_ERROR_AND_REENABLE("INKContCreate");
      return 0;
    }

    /* FC When called back for txn close, the plugins test if the value of the
       continuation data ptr is null or not. Initialize it here to null. */
    INKContDataSet(newcont, NULL);

    if (INKHttpTxnHookAdd(edata, INK_HTTP_TXN_CLOSE_HOOK, newcont) == INK_ERROR) {
      LOG_ERROR_AND_REENABLE("INKHttpTxnHookAdd");
      return 0;
    }

    ret_status = get_client_req(txnp, &url_line, &client_req, &neg_test_val, &pin_val);

    /* FC: added test on url and client req too */
    if ((ret_status == -1) || (url_line == NULL) || (client_req == NULL)) {
      INKDebug(DEBUG_TAG, "Unable to get client request header\n");
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      return 0;
    }

    INKDebug(DEBUG_TAG, "\n%s\n%s", url_line, client_req);
    INKfree(client_req);

    cache_exercise(txnp, url_line, pin_val, neg_test_val, newcont);

    break;

  default:
    ;
  }
  return 0;
}

int
insert_in_response(INKHttpTxn txnp, char *result_val)
{
  INKMBuffer resp_bufp;
  INKMLoc resp_loc;
  INKMLoc field_loc;

  LOG_SET_FUNCTION_NAME("insert_in_response");

#ifdef DEBUG
  if (INKHttpTxnClientRespGet(NULL, &resp_bufp, &resp_loc) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientRespGet(null, buf, hdr_loc)");
  }

  if (INKHttpTxnClientRespGet(txnp, NULL, &resp_loc) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientRespGet(txnp, null, hdr_loc)");
  }

  if (INKHttpTxnClientRespGet(txnp, &resp_bufp, NULL) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientRespGet(txnp, buf, null)");
  }
#endif

  if (!INKHttpTxnClientRespGet(txnp, &resp_bufp, &resp_loc)) {
    LOG_ERROR_AND_RETURN("INKHttpTxnClientRespGet");
  }

  /* create a new field in the response header */
  if ((field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc)) == INK_ERROR_PTR) {
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldCreate");
  }

  /* set its name */
  if (INKMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, "CacheTester-Result", 18) == INK_ERROR) {
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldNameSet");
  }

  /* set its value */
  if (INKMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, field_loc, -1, result_val, strlen(result_val)) == INK_ERROR) {
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldValueIntInsert");
  }

  /* insert it into the header */
  if (INKMimeHdrFieldAppend(resp_bufp, resp_loc, field_loc) == INK_ERROR) {
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldAppend");
  }

  if (INKHandleMLocRelease(resp_bufp, resp_loc, field_loc) == INK_ERROR) {
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
    LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
  }

  if (INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc) == INK_ERROR) {
    LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
  }

  return 1;
}

void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");
  INKCont contp;

  INKDebug(DEBUG_TAG, "INKPluginInit");
  if ((contp = INKContCreate(event_mux, NULL)) == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
  } else if (INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp) == INK_ERROR) {
    LOG_ERROR("INKHttpHookAdd");
  }

}


/* pointers returned by p_url_line and p_client_req should be freed by INKfree() */

int
get_client_req(INKHttpTxn txnp, char **p_url_line, char **p_client_req, int *neg_test_val, int *pin_val)
{
  INKMBuffer bufp;
  INKReturnCode ret_code;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  INKMLoc neg_loc;
  INKMLoc pin_loc;

  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
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
  if (INKHttpTxnClientReqGet(NULL, &bufp, &hdr_loc) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientReqGet(null, buf, hdr_loc)");
  }

  if (INKHttpTxnClientReqGet(txnp, NULL, &hdr_loc) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientReqGet(txnp, null, hdr_loc)");
  }

  if (INKHttpTxnClientReqGet(txnp, &bufp, NULL) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientReqGet(txnp, buf, null)");
  }
#endif

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    LOG_ERROR_AND_RETURN("INKHttpTxnClientReqGet");
  }

  if ((url_loc = INKHttpHdrUrlGet(bufp, hdr_loc)) == INK_ERROR_PTR) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("INKHttpHdrUrlGet");
  }
#ifdef DEBUG
  if (INKUrlStringGet(NULL, url_loc, &url_len) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKUrlStringGet(NULL, url_loc, &int)");
  }

  if (INKUrlStringGet(bufp, NULL, &url_len) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKUrlStringGet(bufp, NULL, &int)");
  }
#endif

  if ((url_str = INKUrlStringGet(bufp, url_loc, &url_len)) == INK_ERROR_PTR) {
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("INKUrlStringGet");
  }

  if (INKHandleMLocRelease(bufp, hdr_loc, url_loc) == INK_ERROR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
  }

  url_str[url_len] = '\0';
  *p_url_line = url_str;

  if ((output_buffer = INKIOBufferCreate()) == INK_ERROR_PTR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    LOG_ERROR_AND_RETURN("INKIOBufferCreate");
  }
#ifdef DEBUG
  if (INKIOBufferReaderAlloc(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferReaderAlloc(NULL)");
  }
#endif

  if ((reader = INKIOBufferReaderAlloc(output_buffer)) == INK_ERROR_PTR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKIOBufferReaderAlloc");
  }

  /* This will print  just MIMEFields and not
     the http request line */
  if (INKMimeHdrPrint(bufp, hdr_loc, output_buffer) == INK_ERROR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKMimeHdrPrint");
  }

  if ((neg_loc = INKMimeHdrFieldFind(bufp, hdr_loc, "CacheTester-HostNameSet", -1)) == INK_ERROR_PTR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldFind");
  }

  if (neg_loc != NULL) {

    ret_code = INKMimeHdrFieldValueIntGet(bufp, hdr_loc, neg_loc, 0, neg_test_val);
    if (ret_code == INK_ERROR) {
      INKfree(url_str);
      INKHandleMLocRelease(bufp, hdr_loc, neg_loc);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
      INKIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("INKMimeHdrFieldValueIntGet");
    }

    if (INKHandleMLocRelease(bufp, hdr_loc, neg_loc) == INK_ERROR) {
      INKfree(url_str);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
      INKIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
    }
  }

  if ((pin_loc = INKMimeHdrFieldFind(bufp, hdr_loc, "CacheTester-Pin", -1)) == INK_ERROR_PTR) {
    INKfree(url_str);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKMimeHdrFieldFind");
  }

  if (pin_loc != NULL) {

    ret_code = INKMimeHdrFieldValueIntGet(bufp, hdr_loc, pin_loc, 0, pin_val);
    if (ret_code == INK_ERROR) {
      INKfree(url_str);
      INKHandleMLocRelease(bufp, hdr_loc, pin_loc);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
      INKIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("INKMimeHdrFieldValueIntGet");
    }

    if (INKHandleMLocRelease(bufp, hdr_loc, pin_loc) == INK_ERROR) {
      INKfree(url_str);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
      INKIOBufferDestroy(output_buffer);
      LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
    }
  }

  if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) == INK_ERROR) {
    INKfree(url_str);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKHandleMLocRelease");
  }

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  if ((total_avail = INKIOBufferReaderAvail(reader)) == INK_ERROR) {
    INKfree(url_str);
    INKIOBufferDestroy(output_buffer);
    LOG_ERROR_AND_RETURN("INKIOBufferReaderAvail");
  }
#ifdef DEBUG
  if (INKIOBufferReaderAvail(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferReaderAvail(NULL)");
  }
#endif

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);

  if (block == INK_ERROR_PTR) {
    INKfree(url_str);
    INKfree(output_string);
    LOG_ERROR_AND_RETURN("INKIOBufferReaderStart");
  }
#ifdef DEBUG
  if (INKIOBufferReaderStart(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferReaderStart(NULL)");
  }

  if (INKIOBufferBlockReadStart(NULL, reader, &block_avail) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferBlockReadStart(null, reader, &int)");
  }

  if (INKIOBufferBlockReadStart(block, NULL, &block_avail) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferBlockReadStart(block, null, &int)");
  }
#endif

  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

    if (block_start == INK_ERROR_PTR) {
      INKfree(url_str);
      INKfree(output_string);
      LOG_ERROR_AND_RETURN("INKIOBufferBlockReadStart");
    }

    /* FC paranoia: make sure we don't copy more bytes than buffer size can handle */
    if ((output_len + block_avail) > total_avail) {
      INKfree(url_str);
      INKfree(output_string);
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
    if (INKIOBufferReaderConsume(reader, block_avail) == INK_ERROR) {
      INKfree(url_str);
      INKfree(output_string);
      LOG_ERROR_AND_RETURN("INKIOBufferReaderConsume");
    }
#ifdef DEBUG
    if (INKIOBufferReaderConsume(NULL, block_avail) != INK_ERROR) {
      LOG_ERROR_NEG("INKIOBufferReaderConsume(null, int)");
    }

    if (INKIOBufferReaderConsume(reader, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKIOBufferReaderConsume(reader, -1)");
    }
#endif

    /* Get the next block now that we've consumed the
       data off the last block */
    if ((block = INKIOBufferReaderStart(reader)) == INK_ERROR_PTR) {
      INKfree(url_str);
      INKfree(output_string);
      LOG_ERROR_AND_RETURN("INKIOBufferReaderStart");
    }
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  /*output_len++; */

  /* Free up the INKIOBuffer that we used to print out the header */
  if (INKIOBufferReaderFree(reader) == INK_ERROR) {
    INKfree(url_str);
    INKfree(output_string);
    LOG_ERROR_AND_RETURN("INKIOBufferReaderFree");
  }
#ifdef DEBUG
  if (INKIOBufferReaderFree(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferReaderFree(NULL)");
  }
#endif

  if (INKIOBufferDestroy(output_buffer) == INK_ERROR) {
    INKfree(url_str);
    INKfree(output_string);
    LOG_ERROR_AND_RETURN("INKIOBufferDestroy");
  }

  *p_client_req = output_string;
  return 1;
}
