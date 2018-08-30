/** @file

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

#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"

#include "ts/ts.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <search.h>

#define PLUGIN_NAME "remap_stats"
#define DEBUG_TAG PLUGIN_NAME

#define MAX_STAT_LENGTH (1 << 8)

typedef struct {
  bool post_remap_host;
  int txn_slot;
  TSStatPersistence persist_type;
  TSMutex stat_creation_mutex;
} config_t;

static void
stat_add(char *name, TSMgmtInt amount, TSStatPersistence persist_type, TSMutex create_mutex)
{
  int stat_id = -1;
  ENTRY search, *result = NULL;
  static __thread struct hsearch_data stat_cache;
  static __thread bool hash_init = false;

  if (unlikely(!hash_init)) {
    hcreate_r(TS_MAX_API_STATS << 1, &stat_cache);
    hash_init = true;
    TSDebug(DEBUG_TAG, "stat cache hash init");
  }

  search.key  = name;
  search.data = 0;
  hsearch_r(search, FIND, &result, &stat_cache);

  if (unlikely(result == NULL)) {
    // This is an unlikely path because we most likely have the stat cached
    // so this mutex won't be much overhead and it fixes a race condition
    // in the RecCore. Hopefully this can be removed in the future.
    TSMutexLock(create_mutex);
    if (TS_ERROR == TSStatFindName((const char *)name, &stat_id)) {
      stat_id = TSStatCreate((const char *)name, TS_RECORDDATATYPE_INT, persist_type, TS_STAT_SYNC_SUM);
      if (stat_id == TS_ERROR) {
        TSDebug(DEBUG_TAG, "Error creating stat_name: %s", name);
      } else {
        TSDebug(DEBUG_TAG, "Created stat_name: %s stat_id: %d", name, stat_id);
      }
    }
    TSMutexUnlock(create_mutex);

    if (stat_id >= 0) {
      search.key  = TSstrdup(name);
      search.data = (void *)((intptr_t)stat_id);
      hsearch_r(search, ENTER, &result, &stat_cache);
      TSDebug(DEBUG_TAG, "Cached stat_name: %s stat_id: %d", name, stat_id);
    }
  } else {
    stat_id = (int)((intptr_t)result->data);
  }

  if (likely(stat_id >= 0)) {
    TSStatIntIncrement(stat_id, amount);
  } else {
    TSDebug(DEBUG_TAG, "stat error! stat_name: %s stat_id: %d", name, stat_id);
  }
}

static char *
get_effective_host(TSHttpTxn txn)
{
  char *effective_url, *tmp;
  const char *host;
  int len;
  TSMBuffer buf;
  TSMLoc url_loc;

  buf = TSMBufferCreate();
  if (TS_SUCCESS != TSUrlCreate(buf, &url_loc)) {
    TSDebug(DEBUG_TAG, "unable to create url");
    TSMBufferDestroy(buf);
    return NULL;
  }
  tmp = effective_url = TSHttpTxnEffectiveUrlStringGet(txn, &len);
  TSUrlParse(buf, url_loc, (const char **)(&tmp), (const char *)(effective_url + len));
  TSfree(effective_url);
  host = TSUrlHostGet(buf, url_loc, &len);
  tmp  = TSstrndup(host, len);
  TSHandleMLocRelease(buf, TS_NULL_MLOC, url_loc);
  TSMBufferDestroy(buf);
  return tmp;
}

static int
handle_read_req_hdr(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  config_t *config;
  void *txnd;

  config = (config_t *)TSContDataGet(cont);
  txnd   = (void *)get_effective_host(txn); // low bit left 0 because we do not know that remap succeeded yet
  TSHttpTxnArgSet(txn, config->txn_slot, txnd);

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  TSDebug(DEBUG_TAG, "Read Req Handler Finished");
  return 0;
}

static int
handle_post_remap(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  config_t *config;
  void *txnd = (void *)0x01; // low bit 1 because we are post remap and thus success

  config = (config_t *)TSContDataGet(cont);

  if (config->post_remap_host) {
    TSHttpTxnArgSet(txn, config->txn_slot, txnd);
  } else {
    txnd = (void *)((uintptr_t)txnd | (uintptr_t)TSHttpTxnArgGet(txn, config->txn_slot)); // We need the hostname pre-remap
    TSHttpTxnArgSet(txn, config->txn_slot, txnd);
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  TSDebug(DEBUG_TAG, "Post Remap Handler Finished");
  return 0;
}

#define CREATE_STAT_NAME(s, h, b) snprintf(s, MAX_STAT_LENGTH, "plugin.%s.%s.%s", PLUGIN_NAME, h, b)

static int
handle_txn_close(TSCont cont, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  config_t *config;
  void *txnd;
  int status_code = 0;
  TSMBuffer buf;
  TSMLoc hdr_loc;
  uint64_t out_bytes, in_bytes;
  char *remap, *hostname;
  char *unknown = "unknown";
  char stat_name[MAX_STAT_LENGTH];

  config = (config_t *)TSContDataGet(cont);
  txnd   = TSHttpTxnArgGet(txn, config->txn_slot);

  hostname = (char *)((uintptr_t)txnd & (~((uintptr_t)0x01))); // Get hostname

  if (txnd) {
    if ((uintptr_t)txnd & 0x01) // remap succeeded?
    {
      if (!config->post_remap_host) {
        remap = hostname;
      } else {
        remap = get_effective_host(txn);
      }

      if (!remap) {
        remap = unknown;
      }

      in_bytes = TSHttpTxnClientReqHdrBytesGet(txn);
      in_bytes += TSHttpTxnClientReqBodyBytesGet(txn);

      CREATE_STAT_NAME(stat_name, remap, "in_bytes");
      stat_add(stat_name, (TSMgmtInt)in_bytes, config->persist_type, config->stat_creation_mutex);

      out_bytes = TSHttpTxnClientRespHdrBytesGet(txn);
      out_bytes += TSHttpTxnClientRespBodyBytesGet(txn);

      CREATE_STAT_NAME(stat_name, remap, "out_bytes");
      stat_add(stat_name, (TSMgmtInt)out_bytes, config->persist_type, config->stat_creation_mutex);

      if (TSHttpTxnClientRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
        status_code = (int)TSHttpHdrStatusGet(buf, hdr_loc);
        TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

        if (status_code < 200) {
          CREATE_STAT_NAME(stat_name, remap, "status_other");
        } else if (status_code <= 299) {
          CREATE_STAT_NAME(stat_name, remap, "status_2xx");
        } else if (status_code <= 399) {
          CREATE_STAT_NAME(stat_name, remap, "status_3xx");
        } else if (status_code <= 499) {
          CREATE_STAT_NAME(stat_name, remap, "status_4xx");
        } else if (status_code <= 599) {
          CREATE_STAT_NAME(stat_name, remap, "status_5xx");
        } else {
          CREATE_STAT_NAME(stat_name, remap, "status_other");
        }

        stat_add(stat_name, 1, config->persist_type, config->stat_creation_mutex);
      } else {
        CREATE_STAT_NAME(stat_name, remap, "status_unknown");
        stat_add(stat_name, 1, config->persist_type, config->stat_creation_mutex);
      }

      if (remap != unknown) {
        TSfree(remap);
      }
    } else if (hostname) {
      TSfree(hostname);
    }
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  TSDebug(DEBUG_TAG, "Handler Finished");
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont pre_remap_cont, post_remap_cont, global_cont;
  config_t *config;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[remap_stats] Plugin registration failed");

    return;
  } else {
    TSDebug(DEBUG_TAG, "Plugin registration succeeded");
  }

  config                      = TSmalloc(sizeof(config_t));
  config->post_remap_host     = false;
  config->persist_type        = TS_STAT_NON_PERSISTENT;
  config->stat_creation_mutex = TSMutexCreate();

  if (argc > 1) {
    int c;
    static const struct option longopts[] = {
      {"post-remap-host", no_argument, NULL, 'P'}, {"persistent", no_argument, NULL, 'p'}, {NULL, 0, NULL, 0}};

    while ((c = getopt_long(argc, (char *const *)argv, "Pp", longopts, NULL)) != -1) {
      switch (c) {
      case 'P':
        config->post_remap_host = true;
        TSDebug(DEBUG_TAG, "Using post remap hostname");
        break;
      case 'p':
        config->persist_type = TS_STAT_PERSISTENT;
        TSDebug(DEBUG_TAG, "Using persistent stats");
        break;
      default:
        break;
      }
    }
  }

  TSHttpTxnArgIndexReserve(PLUGIN_NAME, "txn data", &(config->txn_slot));

  if (!config->post_remap_host) {
    pre_remap_cont = TSContCreate(handle_read_req_hdr, NULL);
    TSContDataSet(pre_remap_cont, (void *)config);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, pre_remap_cont);
  }

  post_remap_cont = TSContCreate(handle_post_remap, NULL);
  TSContDataSet(post_remap_cont, (void *)config);
  TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, post_remap_cont);

  global_cont = TSContCreate(handle_txn_close, NULL);
  TSContDataSet(global_cont, (void *)config);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, global_cont);

  TSDebug(DEBUG_TAG, "Init complete");
}
