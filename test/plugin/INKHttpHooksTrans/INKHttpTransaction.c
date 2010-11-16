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


#include "ts.h"


static int
handle_HTTP_SEND_RESPONSE_HDR(TSCont contp, TSEvent event, void *eData)
{
  TSMBuffer buffer;
  TSMLoc buffOffset;
  TSHttpTxn txnp = (TSHttpTxn) contp;
  int re = 0, err = 0;
  re = TSHttpTxnCachedReqGet(txnp, &buffer, &buffOffset);
  if (re) {
    TSDebug("TSHttpTransaction", "TSHttpTxnCachedReqGet(): TS_EVENT_HTTP_SEND_RESPONSE_HDR, and txnp set\n");
    /* Display all buffer contents */

  } else {
    TSDebug("TSHttpTransaction", "TSHttpTxnCachedReqGet(): Failed.");
    err++;
  }

/*
TSHttpTxnCachedRespGet (TSHttpTxn txnp, TSMBuffer *bufp, TSMLoc *offset);
*/
/* Display buffer contents */

/* Other API calls that should process this event */
/* Display results */

  return err;
}

static int
handle_READ_REQUEST_HDR(TSCont cont, TSEvent event, void *eData)
{
  int err = 0;
  return err;
}

static int
handle_READ_RESPONSE_HDR(TSCont contp, TSEvent event, void *eData)
{
  int err = 0;
  return err;
}

static int
TSHttpTransaction(TSCont contp, TSEvent event, void *eData)
{
  TSHttpSsn ssnp = (TSHttpSsn) eData;
  TSHttpTxn txnp = (TSHttpTxn) eData;

  switch (event) {

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_HTTP_SEND_RESPONSE_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    handle_READ_REQUEST_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_READ_RESPONSE_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp = TSContCreate(TSHttpTransaction, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
}
