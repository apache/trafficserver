/** @file

  An example plugin that denies client access to blacklisted sites (blacklist.txt).

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

#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "blacklist_1"

#define MAX_NSITES 500
#define RETRY_TIME 10

static char *sites[MAX_NSITES];
static int nsites;
static TSMutex sites_mutex;
static TSTextLogObject log;
static TSCont global_contp;

static void handle_txn_start(TSCont contp, TSHttpTxn txnp);

typedef struct contp_data {
  enum calling_func {
    HANDLE_DNS,
    HANDLE_RESPONSE,
    READ_BLACKLIST,
  } cf;

  TSHttpTxn txnp;

} cdata;

static void
destroy_continuation(TSHttpTxn txnp, TSCont contp)
{
  cdata *cd = NULL;

  cd = (cdata *)TSContDataGet(contp);
  if (cd != NULL) {
    TSfree(cd);
  }
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return;
}

static void
handle_dns(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  const char *host;
  int i;
  int host_length;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    goto done;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = TSUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    TSError("[%s] Couldn't retrieve request hostname", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  /* We need to lock the sites_mutex as that is the mutex that is
     protecting the global list of all blacklisted sites. */
  if (TSMutexLockTry(sites_mutex) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "Unable to get lock. Will retry after some time");
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSContSchedule(contp, RETRY_TIME, TS_THREAD_POOL_DEFAULT);
    return;
  }

  for (i = 0; i < nsites; i++) {
    if (strncmp(host, sites[i], host_length) == 0) {
      if (log) {
        TSTextLogObjectWrite(log, "blacklisting site: %s", sites[i]);
      } else {
        TSDebug(PLUGIN_NAME, "blacklisting site: %s", sites[i]);
      }
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      TSMutexUnlock(sites_mutex);
      return;
    }
  }

  TSMutexUnlock(sites_mutex);
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
handle_response(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  char *url_str;
  char *buf;
  int url_length;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client response header", PLUGIN_NAME);
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_FORBIDDEN);
  TSHttpHdrReasonSet(bufp, hdr_loc, TSHttpHdrReasonLookup(TS_HTTP_STATUS_FORBIDDEN),
                     strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_FORBIDDEN)));

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  buf = (char *)TSmalloc(4096);

  url_str = TSUrlStringGet(bufp, url_loc, &url_length);
  sprintf(buf, "You are forbidden from accessing \"%s\"\n", url_str);
  TSfree(url_str);
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
read_blacklist(TSCont contp)
{
  char blacklist_file[1024];
  TSFile file;

  sprintf(blacklist_file, "%s/blacklist.txt", TSPluginDirGet());
  file   = TSfopen(blacklist_file, "r");
  nsites = 0;

  /* If the Mutex lock is not successful try again in RETRY_TIME */
  if (TSMutexLockTry(sites_mutex) != TS_SUCCESS) {
    if (file != NULL) {
      TSfclose(file);
    }
    TSContSchedule(contp, RETRY_TIME, TS_THREAD_POOL_DEFAULT);
    return;
  }

  if (file != NULL) {
    char buffer[1024];

    while (TSfgets(file, buffer, sizeof(buffer) - 1) != NULL && nsites < MAX_NSITES) {
      char *eol;
      if ((eol = strstr(buffer, "\r\n")) != NULL) {
        /* To handle newlines on Windows */
        *eol = '\0';
      } else if ((eol = strchr(buffer, '\n')) != NULL) {
        *eol = '\0';
      } else {
        /* Not a valid line, skip it */
        continue;
      }
      if (sites[nsites] != NULL) {
        TSfree(sites[nsites]);
      }
      sites[nsites] = TSstrdup(buffer);
      nsites++;
    }

    TSfclose(file);
  } else {
    TSError("[%s] Unable to open %s", PLUGIN_NAME, blacklist_file);
    TSError("[%s] All sites will be allowed", PLUGIN_NAME);
  }

  TSMutexUnlock(sites_mutex);
}

static int
blacklist_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp;
  cdata *cd;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn)edata;
    handle_txn_start(contp, txnp);
    return 0;
  case TS_EVENT_HTTP_OS_DNS:
    if (contp != global_contp) {
      cd     = (cdata *)TSContDataGet(contp);
      cd->cf = HANDLE_DNS;
      handle_dns(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case TS_EVENT_HTTP_TXN_CLOSE:
    txnp = (TSHttpTxn)edata;
    if (contp != global_contp) {
      destroy_continuation(txnp, contp);
    }
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    if (contp != global_contp) {
      cd     = (cdata *)TSContDataGet(contp);
      cd->cf = HANDLE_RESPONSE;
      handle_response(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case TS_EVENT_TIMEOUT:
    /* when mutex lock is not acquired and continuation is rescheduled,
       the plugin is called back with TS_EVENT_TIMEOUT with a NULL
       edata. We need to decide, in which function did the MutexLock
       failed and call that function again */
    if (contp != global_contp) {
      cd = (cdata *)TSContDataGet(contp);
      switch (cd->cf) {
      case HANDLE_DNS:
        handle_dns(cd->txnp, contp);
        return 0;
      case HANDLE_RESPONSE:
        handle_response(cd->txnp, contp);
        return 0;
      default:
        TSDebug(PLUGIN_NAME, "This event was unexpected: %d", event);
        break;
      }
    } else {
      read_blacklist(contp);
      return 0;
    }
  default:
    break;
  }
  return 0;
}

static void
handle_txn_start(TSCont contp ATS_UNUSED, TSHttpTxn txnp)
{
  TSCont txn_contp;
  cdata *cd;

  txn_contp = TSContCreate((TSEventFunc)blacklist_plugin, TSMutexCreate());
  /* create the data that'll be associated with the continuation */
  cd = (cdata *)TSmalloc(sizeof(cdata));
  TSContDataSet(txn_contp, cd);

  cd->txnp = txnp;

  TSHttpTxnHookAdd(txnp, TS_HTTP_OS_DNS_HOOK, txn_contp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  int i;
  TSPluginRegistrationInfo info;
  TSReturnCode error;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  /* create an TSTextLogObject to log blacklisted requests to */
  error = TSTextLogObjectCreate("blacklist", TS_LOG_MODE_ADD_TIMESTAMP, &log);
  if (!log || error == TS_ERROR) {
    TSDebug(PLUGIN_NAME, "error while creating log");
  }

  sites_mutex = TSMutexCreate();

  nsites = 0;
  for (i = 0; i < MAX_NSITES; i++) {
    sites[i] = NULL;
  }

  global_contp = TSContCreate(blacklist_plugin, sites_mutex);
  read_blacklist(global_contp);

  /*TSHttpHookAdd (TS_HTTP_OS_DNS_HOOK, contp); */
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, global_contp);
}
