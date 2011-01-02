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

/* blacklist-1.c:  an example program that denies client access
 *                 to blacklisted sites. This plugin illustrates
 *                 how to use configuration information from a
 *                 configuration file (blacklist.txt) that can be
 *                 updated through the Traffic Manager UI.
 *
 *
 *	Usage:
 *	(NT) : BlackList.dll
 *	(Solaris) : blacklist-1.so
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <ts/ts.h>

#define MAX_NSITES 500
#define RETRY_TIME 10

#define PLUGIN_NAME "blacklist-1-neg"
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

static char *sites[MAX_NSITES];
static int nsites;
static TSMutex sites_mutex;
static TSTextLogObject log;
static TSCont global_contp;

static void handle_txn_start(TSCont contp, TSHttpTxn txnp);

typedef struct contp_data
{

  enum calling_func
  {
    HANDLE_DNS,
    HANDLE_RESPONSE,
    READ_BLACKLIST
  } cf;

  TSHttpTxn txnp;

} cdata;


static void
handle_dns(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  const char *host;
  int i;
  int host_length;
  cdata *cd;

  if (!TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSError("couldn't retrieve client request header\n");
    goto done;
  }

  url_loc = TSHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    TSError("couldn't retrieve request url\n");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = TSUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    TSError("couldn't retrieve request hostname\n");
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  /*  the continuation we are dealing here is created for each transaction.
     since the transactions themselves have a mutex associated to them,
     we don't need to lock that mutex explicitly. */
  for (i = 0; i < nsites; i++) {
    if (strncmp(host, sites[i], host_length) == 0) {
      if (log) {
        TSTextLogObjectWrite(log, "blacklisting site: %s", sites[i]);
      } else {
        TSDebug("blacklist-1", "blacklisting site: %s\n", sites[i]);
      }
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      TSMutexUnlock(sites_mutex);
      return;
    }
  }

  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  /* If not a blacklist site, then destroy the continuation created for
     this transaction */
  cd = (cdata *) TSContDataGet(contp);
  TSfree(cd);
  TSContDestroy(contp);
}

static void
handle_response(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  char *url_str;
  char *buf;
  int url_length;
  cdata *cd;

  LOG_SET_FUNCTION_NAME("handle_response");

  if (!TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    TSError("couldn't retrieve client response header\n");
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_FORBIDDEN);
  TSHttpHdrReasonSet(bufp, hdr_loc,
                      TSHttpHdrReasonLookup(TS_HTTP_STATUS_FORBIDDEN),
                      strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_FORBIDDEN)));

  if (!TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSError("couldn't retrieve client request header\n");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  url_loc = TSHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    TSError("couldn't retrieve request url\n");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  buf = (char *) TSmalloc(4096);

  url_str = TSUrlStringGet(bufp, url_loc, &url_length);
  sprintf(buf, "You are forbidden from accessing \"%s\"\n", url_str);
  TSfree(url_str);
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);

  /* negative test for TSHttpTxnErrorBodySet */
#ifdef DEBUG
  if (TSHttpTxnErrorBodySet(NULL, buf, strlen(buf), NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnErrorBodySet");
  }
  if (TSHttpTxnErrorBodySet(txnp, NULL, 10, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpTxnErrorBodySet");
  }
#endif

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  /* After everything's done, Destroy the continuation
     created for this transaction */
  cd = (cdata *) TSContDataGet(contp);
  TSfree(cd);
  TSContDestroy(contp);
}

static void
read_blacklist(TSCont contp)
{
  char blacklist_file[1024];
  TSFile file;
  int lock;
  TSReturnCode ret_code;

  LOG_SET_FUNCTION_NAME("read_blacklist");
  sprintf(blacklist_file, "%s/blacklist.txt", TSPluginDirGet());
  file = TSfopen(blacklist_file, "r");

  ret_code = TSMutexLockTry(sites_mutex, &lock);

  if (ret_code == TS_ERROR) {
    TSError("Failed to lock mutex. Cannot read new blacklist file. Exiting ...\n");
    return;
  }
  nsites = 0;

  /* If the Mutext lock is not successful try again in RETRY_TIME */
  if (!lock) {
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
    TSError("unable to open %s\n", blacklist_file);
    TSError("all sites will be allowed\n", blacklist_file);
  }

  TSMutexUnlock(sites_mutex);

  /* negative test for TSContSchedule */
#ifdef DEBUG
  if (TSContSchedule(NULL, 10, TS_THREAD_POOL_DEFAULT) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSContSchedule");
  }
#endif

}

static int
blacklist_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp;
  cdata *cd;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn) edata;
    handle_txn_start(contp, txnp);
    return 0;
  case TS_EVENT_HTTP_OS_DNS:
    if (contp != global_contp) {
      cd = (cdata *) TSContDataGet(contp);
      cd->cf = HANDLE_DNS;
      handle_dns(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    if (contp != global_contp) {
      cd = (cdata *) TSContDataGet(contp);
      cd->cf = HANDLE_RESPONSE;
      handle_response(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case TS_EVENT_MGMT_UPDATE:
    if (contp == global_contp) {
      read_blacklist(contp);
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
      cd = (cdata *) TSContDataGet(contp);
      switch (cd->cf) {
      case HANDLE_DNS:
        handle_dns(cd->txnp, contp);
        return 0;
      case HANDLE_RESPONSE:
        handle_response(cd->txnp, contp);
        return 0;
      default:
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
handle_txn_start(TSCont contp, TSHttpTxn txnp)
{
  TSCont txn_contp;
  cdata *cd;

  txn_contp = TSContCreate((TSEventFunc) blacklist_plugin, TSMutexCreate());
  /* create the data that'll be associated with the continuation */
  cd = (cdata *) TSmalloc(sizeof(cdata));
  TSContDataSet(txn_contp, cd);

  cd->txnp = txnp;

  TSHttpTxnHookAdd(txnp, TS_HTTP_OS_DNS_HOOK, txn_contp);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}


int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  int i;
  TSPluginRegistrationInfo info;
  TSReturnCode error;

  LOG_SET_FUNCTION_NAME("TSPluginInit");
  info.plugin_name = "blacklist-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!TSPluginRegister(TS_SDK_VERSION_3_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  /* create an TSTextLogObject to log blacklisted requests to */
  error = TSTextLogObjectCreate("blacklist", TS_LOG_MODE_ADD_TIMESTAMP, &log);
  if (!log || error == TS_ERROR) {
    TSDebug("blacklist-1", "error while creating log");
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

  TSMgmtUpdateRegister(global_contp, "Inktomi Blacklist Plugin", "blacklist.cgi");

#ifdef DEBUG
  /* negative test for TSMgmtUpdateRegister */
  if (TSMgmtUpdateRegister(NULL, "Inktomi Blacklist Plugin", "blacklist.cgi") != TS_ERROR) {
    LOG_ERROR_NEG("TSMgmtUpdateRegister");
  }
  if (TSMgmtUpdateRegister(global_contp, NULL, "blacklist.cgi") != TS_ERROR) {
    LOG_ERROR_NEG("TSMgmtUpdateRegister");
  }
  if (TSMgmtUpdateRegister(global_contp, "Inktomi Blacklist Plugin", NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMgmtUpdateRegister");
  }

  /* negative test for TSIOBufferReaderClone & TSVConnAbort */
  if (TSIOBufferReaderClone(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferReaderClone");
  }

  if (TSVConnAbort(NULL, 1) != TS_ERROR) {
    LOG_ERROR_NEG("TSVConnAbort");
  }
#endif
}
