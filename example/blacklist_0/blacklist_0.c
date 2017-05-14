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
 *   blacklist_0.c:
 *	original version of blacklist-1, now used for internal testing
 *
 *
 *	Usage:
 *
 */

#include <stdio.h>
#include <string.h>
#include <ts/ts.h>

#define PLUGIN_NAME "blacklist_0"

static char **sites;
static int nsites;

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
  for (i = 0; i < nsites; i++) {
    if (strncmp(host, sites[i], host_length) == 0) {
      printf("blacklisting site: %s\n", sites[i]);
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      return;
    }
  }
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
handle_response(TSHttpTxn txnp)
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

  buf = TSmalloc(4096);

  url_str = TSUrlStringGet(bufp, url_loc, &url_length);
  sprintf(buf, "You are forbidden from accessing \"%s\"\n", url_str);
  TSfree(url_str);
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
blacklist_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_response(txnp);
    return 0;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  int i;
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  nsites = argc - 1;
  if (nsites > 0) {
    sites = (char **)TSmalloc(sizeof(char *) * nsites);

    for (i = 0; i < nsites; i++) {
      sites[i] = TSstrdup(argv[i + 1]);
    }

    TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, TSContCreate(blacklist_plugin, NULL));
  }
}
