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

/* extauth.c */

#include <stdio.h>
#include <netinet/in.h>
#include "ts.h"

typedef struct
{
  TSHttpTxn txn;
  TSCont cont;
  TSAction act;
  TSIOBuffer abuf;
  TSIOBufferReader areader;
  TSIOBuffer rbuf;
  TSIOBufferReader rreader;
  TSVConn avc;
  TSVIO avio;
} AuthData;

static int auth_plugin(TSCont, TSEvent, void *);
static void check_auth(TSHttpTxn, TSCont);
static void require_auth(TSHttpTxn);
static int verify_auth(TSCont, TSEvent, void *);
static void destroy_auth(TSCont);

static in_addr_t svrip;
static int svrport;

void
TSPluginInit(int argc, const char *argv[])
{

  svrip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  svrip = htonl(svrip);
  svrport = 7;

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(auth_plugin, NULL));
}

static int
auth_plugin(TSCont contp, TSEvent event, void *edata)
{

  TSHttpTxn txnp = (TSHttpTxn *) edata;

  TSDebug("extauth", "auth_plugin: entered");

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    check_auth(txnp, contp);
    return 0;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    require_auth(txnp);
    return 0;
  default:
    break;
  }

  return 0;
}

static void
check_auth(TSHttpTxn txnp, TSCont contp)
{

  AuthData *adata;
  TSCont acontp;
  TSAction action;

  TSDebug("extauth", "check_auth: entered");

  adata = (AuthData *) TSmalloc(sizeof(AuthData));
  adata->txn = txnp;
  adata->cont = contp;
  adata->act = NULL;
  adata->abuf = NULL;
  adata->areader = NULL;
  adata->rbuf = NULL;
  adata->rreader = NULL;
  adata->avc = NULL;
  adata->avio = NULL;
  acontp = TSContCreate(verify_auth, TSMutexCreate());
  TSContDataSet(acontp, adata);

  action = TSNetConnect(acontp, svrip, svrport);
  if (!TSActionDone(action)) {
    adata->act = action;
  }

  TSDebug("extauth", "check_auth: TSNetConnect called");
  TSDebug("extauth", "check_auth: returning");

  return;
}

static int
verify_auth(TSCont acontp, TSEvent event, void *edata)
{

  AuthData *adata;
  TSIOBufferData d;
  TSIOBufferBlock b;
  char *userinfo = "good:evil";
  int uil = strlen(userinfo);

  TSDebug("extauth", "verify_auth: entered");

  adata = (AuthData *) TSContDataGet(acontp);

  switch (event) {
  case TS_EVENT_NET_CONNECT:

    TSDebug("extauth", "verify_auth: NET_CONNECT");

    adata->act = NULL;
    adata->abuf = TSIOBufferCreate();
    adata->areader = TSIOBufferReaderAlloc(adata->abuf);
    d = TSIOBufferDataCreate(userinfo, uil, TS_DATA_CONSTANT);
    b = TSIOBufferBlockCreate(d, uil, 0);
    TSIOBufferAppend(adata->abuf, b);

    adata->avc = (TSVConn) edata;
    adata->avio = TSVConnWrite(adata->avc, acontp, adata->areader, TSIOBufferReaderAvail(adata->areader));
    return 0;
  case TS_EVENT_VCONN_WRITE_READY:

    TSDebug("extauth", "verify_auth: VCONN_WRITE_READY");

    TSVIOReenable(adata->avio);
    return 0;
  case TS_EVENT_VCONN_WRITE_COMPLETE:

    TSDebug("extauth", "verify_auth: VCONN_WRITE_COMPLETE");

    TSVConnShutdown(adata->avc, 0, 1);
    adata->rbuf = TSIOBufferCreate();
    adata->rreader = TSIOBufferReaderAlloc(adata->rbuf);
    adata->avio = TSVConnRead(adata->avc, acontp, adata->rbuf, uil);
    return 0;
  case TS_EVENT_VCONN_READ_READY:

    TSDebug("extauth", "verify_auth: VCONN_READ_READY");

    TSVIOReenable(adata->avio);
    return 0;
  case TS_EVENT_VCONN_READ_COMPLETE:

    TSDebug("extauth", "verify_auth: VCONN_READ_COMPLETE");

    if (TSIOBufferReaderAvail(adata->rreader) == uil) {
      TSIOBufferBlock rb;
      const char *resp;
      char *respstr;
      int avail, i;

      rb = TSIOBufferReaderStart(adata->rreader);
      resp = TSIOBufferBlockReadStart(rb, adata->rreader, &avail);
      if (avail == uil) {
        respstr = (char *) TSmalloc(sizeof(char) * (uil + 1));
        for (i = 0; i < uil; i++) {
          respstr[i] = resp[i];
        }
        respstr[i] = '\0';
        TSIOBufferReaderConsume(adata->rreader, uil);
        TSDebug("extauth", "AuthServer Response - %s", respstr);
        TSfree(respstr);
      }
      TSIOBufferDestroy(adata->rbuf);
      adata->rbuf = NULL;
      adata->rreader = NULL;
      TSVConnClose(adata->avc);
      adata->avc = NULL;
      adata->avio = NULL;

      TSHttpTxnReenable(adata->txn, TS_EVENT_HTTP_CONTINUE);
      destroy_auth(acontp);
      return 0;
    }
    break;
  case TS_EVENT_NET_CONNECT_FAILED:

    TSDebug("extauth", "verify_auth: NET_CONNECT_FAILED");

    adata->act = NULL;
    break;
  case TS_EVENT_ERROR:

    TSDebug("extauth", "verify_auth: ERROR");

    break;
  default:
    break;
  }

  TSHttpTxnHookAdd(adata->txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, adata->cont);
  TSHttpTxnReenable(adata->txn, TS_EVENT_HTTP_ERROR);

  destroy_auth(acontp);
  return 0;
}

static void
destroy_auth(TSCont acontp)
{

  AuthData *adata;

  adata = TSContDataGet(acontp);

  if (adata->abuf) {
    TSIOBufferDestroy(adata->abuf);
  }
  if (adata->rbuf) {
    TSIOBufferDestroy(adata->rbuf);
  }
  if (adata->act) {
    TSActionCancel(adata->act);
  }
  if (adata->avc) {
    TSVConnAbort(adata->avc, 1);
  }

  TSfree(adata);
  TSContDestroy(acontp);
  return;
}

static void
require_auth(TSHttpTxn txnp)
{

  TSMBuffer bufp;
  TSMLoc hdr_loc, newfld_loc;

  if (!TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    TSError("require_auth: failed to retrieve client response");
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  TSHttpHdrReasonSet(bufp, hdr_loc, TSHttpHdrReasonLookup(TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED), -1);

  newfld_loc = TSMimeHdrFieldCreate(bufp, hdr_loc);
  TSMimeHdrFieldNameSet(bufp, hdr_loc, newfld_loc, TS_MIME_FIELD_PROXY_AUTHENTICATE, TS_MIME_LEN_PROXY_AUTHENTICATE);
  TSMimeHdrFieldValueAppend(bufp, hdr_loc, newfld_loc, "Basic realm=\"Armageddon\"", -1);
  TSMimeHdrFieldAppend(bufp, hdr_loc, newfld_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}
