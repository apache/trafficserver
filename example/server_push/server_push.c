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
 *   server_push.c:
 *    an example of server push.
 *
 *
 *	Usage:
 * 	  server_push.so http://example.com/favicon.ico
 *
 *
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ts/ts.h"
#include "ts/experimental.h"
#include "tscore/ink_defs.h"

const char *PLUGIN_NAME = "server_push";

char url[256];

bool
should_push(TSHttpTxn txnp)
{
  TSMBuffer mbuf;
  TSMLoc hdr, url;
  if (TSHttpTxnClientReqGet(txnp, &mbuf, &hdr) != TS_SUCCESS) {
    return false;
  }
  if (TSHttpHdrUrlGet(mbuf, hdr, &url) != TS_SUCCESS) {
    return false;
  }
  int len;
  TSUrlHttpQueryGet(mbuf, url, &len);
  TSHandleMLocRelease(mbuf, hdr, url);
  TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdr);
  if (len > 0) {
    return true;
  } else {
    return false;
  }
}

static int
server_push_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp;
  TSHttpTxn txnp;

  switch (event) {
  case TS_EVENT_HTTP_SSN_START:
    ssnp = (TSHttpSsn)edata;
    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, contp);
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_TXN_START:
    txnp = (TSHttpTxn)edata;
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    txnp = (TSHttpTxn)edata;
    if (should_push(txnp)) {
      TSHttpTxnServerPush(txnp, url, strlen(url));
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }

  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  TSstrlcpy(url, argv[1], sizeof(url));
  TSCont handler = TSContCreate(server_push_plugin, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, handler);
}
