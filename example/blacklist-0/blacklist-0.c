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
 *   blacklist-0.c:  
 *	original version of blacklist-1, now used for internal testing
 *
 *
 *	Usage:	
 *
 */

#include <stdio.h>
#include <string.h>
#include "InkAPI.h"

static char **sites;
static int nsites;

static void
handle_dns(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  const char *host;
  int i;
  int host_length;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = INKUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    INKError("couldn't retrieve request hostname\n");
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }
  for (i = 0; i < nsites; i++) {
    if (strncmp(host, sites[i], host_length) == 0) {
      printf("blacklisting site: %s\n", sites[i]);
      INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      INKHandleStringRelease(bufp, url_loc, host);
      INKHandleMLocRelease(bufp, hdr_loc, url_loc);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, url_loc);
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);
      return;
    }
  }
  INKHandleStringRelease(bufp, url_loc, host);
  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static void
handle_response(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  char *url_str;
  char *buf;
  int url_length;

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client response header\n");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_FORBIDDEN);
  INKHttpHdrReasonSet(bufp, hdr_loc,
                      INKHttpHdrReasonLookup(INK_HTTP_STATUS_FORBIDDEN),
                      strlen(INKHttpHdrReasonLookup(INK_HTTP_STATUS_FORBIDDEN)));

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  buf = INKmalloc(4096);

  url_str = INKUrlStringGet(bufp, url_loc, &url_length);
  sprintf(buf, "You are forbidden from accessing \"%s\"\n", url_str);
  INKfree(url_str);
  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  INKHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
blacklist_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_response(txnp);
    return 0;
  default:
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }

  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  int i;
  INKPluginRegistrationInfo info;

  info.plugin_name = "blacklist-0";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("Plugin registration failed.\n");

  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 5.2.0 or later\n");
    return;
  }


  nsites = argc - 1;
  if (nsites > 0) {
    sites = (char **) INKmalloc(sizeof(char *) * nsites);

    for (i = 0; i < nsites; i++) {
      sites[i] = INKstrdup(argv[i + 1]);
    }

    INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, INKContCreate(blacklist_plugin, NULL));
  }
}
