/** @file

  A plugin that sets custom 204 response bodies.

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

#include "ts/ts.h"
#include "string.h"

#define PLUGIN_NAME "custom204plugintest"

static int
local_handler(TSCont contp, TSEvent event, void *edata)
{
  const char *msg = "<HTML>\n"
                    "<HEAD>\n"
                    "<TITLE>Spec-breaking 204!</TITLE>\n"
                    "</HEAD>\n"
                    "\n"
                    "<BODY>\n"
                    "<H1>This is body content for a 204.</H1>\n"
                    "<HR>\n"
                    "\n"
                    "Description: According to rfc7231 I should not have been sent to you!<BR/>\n"
                    "This response was sent via the custom204plugin via a call to TSHttpTxnErrorBodySet.\n"
                    "<HR>\n"
                    "</BODY>";
  TSHttpTxn txnp = (TSHttpTxn)edata;
  TSMBuffer bufp = nullptr;
  TSMLoc hdr_loc = nullptr;
  TSMLoc url_loc = nullptr;
  ;
  const char *host = nullptr;
  int host_length;
  const char *test_host = "www.customplugin204.test";

  switch (event) {
  case TS_EVENT_HTTP_PRE_REMAP:
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_PRE_REMAP received");
    TSDebug(PLUGIN_NAME, "running plugin logic.");
    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "Couldn't retrieve client request header");
      TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
      goto done;
    }
    TSDebug(PLUGIN_NAME, "got client request");

    if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
      TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
      TSDebug(PLUGIN_NAME, "Couldn't retrieve request url");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      goto done;
    }
    TSDebug(PLUGIN_NAME, "got client request url");

    host = TSUrlHostGet(bufp, url_loc, &host_length);
    if (!host) {
      TSError("[%s] Couldn't retrieve request hostname", PLUGIN_NAME);
      TSDebug(PLUGIN_NAME, "Couldn't retrieve request hostname");
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      goto done;
    }
    TSDebug(PLUGIN_NAME, "request's host was retrieved");

    if (strncmp(host, test_host, strlen(test_host)) == 0) {
      TSDebug(PLUGIN_NAME, "host matches, hook TS_HTTP_SEND_RESPONSE_HDR_HOOK");
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
    TSDebug(PLUGIN_NAME, "Host != expected host '%s'", test_host);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    TSDebug(PLUGIN_NAME, "Returning 204 with custom response body.");
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_NO_CONTENT);
    TSHttpTxnErrorBodySet(txnp, TSstrdup(msg), strlen(msg), TSstrdup("text/html"));
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_TXN_CLOSE received");
    TSContDestroy(contp);
    break;

  default:
    TSAssert(!"Unexpected event");
    break;
  }

done:
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 1;
}

static int
global_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp   = (TSHttpTxn)edata;
  TSCont txn_contp = nullptr;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    txn_contp = TSContCreate(local_handler, TSMutexCreate());
    TSHttpTxnHookAdd(txnp, TS_HTTP_PRE_REMAP_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
    TSDebug(PLUGIN_NAME, "hooked TS_HTTP_OS_DNS_HOOK and TS_EVENT_HTTP_TXN_CLOSE_HOOK");
    break;
  default:
    TSAssert(!"Unexpected event");
    break;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  TSCont contp = TSContCreate(global_handler, TSMutexCreate());
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp);
}
