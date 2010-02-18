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
  INKHttpTxn txn;
  INKCont cont;
  INKAction act;
  INKIOBuffer abuf;
  INKIOBufferReader areader;
  INKIOBuffer rbuf;
  INKIOBufferReader rreader;
  INKVConn avc;
  INKVIO avio;
} AuthData;

static int auth_plugin(INKCont, INKEvent, void *);
static void check_auth(INKHttpTxn, INKCont);
static void require_auth(INKHttpTxn);
static int verify_auth(INKCont, INKEvent, void *);
static void destroy_auth(INKCont);

static in_addr_t svrip;
static int svrport;

void
INKPluginInit(int argc, const char *argv[])
{

  svrip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  svrip = htonl(svrip);
  svrport = 7;

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(auth_plugin, NULL));
}

static int
auth_plugin(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn *) edata;

  INKDebug("extauth", "auth_plugin: entered");

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    check_auth(txnp, contp);
    return 0;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    require_auth(txnp);
    return 0;
  default:
    break;
  }

  return 0;
}

static void
check_auth(INKHttpTxn txnp, INKCont contp)
{

  AuthData *adata;
  INKCont acontp;
  INKAction action;

  INKDebug("extauth", "check_auth: entered");

  adata = (AuthData *) INKmalloc(sizeof(AuthData));
  adata->txn = txnp;
  adata->cont = contp;
  adata->act = NULL;
  adata->abuf = NULL;
  adata->areader = NULL;
  adata->rbuf = NULL;
  adata->rreader = NULL;
  adata->avc = NULL;
  adata->avio = NULL;
  acontp = INKContCreate(verify_auth, INKMutexCreate());
  INKContDataSet(acontp, adata);

  action = INKNetConnect(acontp, svrip, svrport);
  if (!INKActionDone(action)) {
    adata->act = action;
  }

  INKDebug("extauth", "check_auth: INKNetConnect called");
  INKDebug("extauth", "check_auth: returning");

  return;
}

static int
verify_auth(INKCont acontp, INKEvent event, void *edata)
{

  AuthData *adata;
  INKIOBufferData d;
  INKIOBufferBlock b;
  char *userinfo = "good:evil";
  int uil = strlen(userinfo);

  INKDebug("extauth", "verify_auth: entered");

  adata = (AuthData *) INKContDataGet(acontp);

  switch (event) {
  case INK_EVENT_NET_CONNECT:

    INKDebug("extauth", "verify_auth: NET_CONNECT");

    adata->act = NULL;
    adata->abuf = INKIOBufferCreate();
    adata->areader = INKIOBufferReaderAlloc(adata->abuf);
    d = INKIOBufferDataCreate(userinfo, uil, INK_DATA_CONSTANT);
    b = INKIOBufferBlockCreate(d, uil, 0);
    INKIOBufferAppend(adata->abuf, b);

    adata->avc = (INKVConn) edata;
    adata->avio = INKVConnWrite(adata->avc, acontp, adata->areader, INKIOBufferReaderAvail(adata->areader));
    return 0;
  case INK_EVENT_VCONN_WRITE_READY:

    INKDebug("extauth", "verify_auth: VCONN_WRITE_READY");

    INKVIOReenable(adata->avio);
    return 0;
  case INK_EVENT_VCONN_WRITE_COMPLETE:

    INKDebug("extauth", "verify_auth: VCONN_WRITE_COMPLETE");

    INKVConnShutdown(adata->avc, 0, 1);
    adata->rbuf = INKIOBufferCreate();
    adata->rreader = INKIOBufferReaderAlloc(adata->rbuf);
    adata->avio = INKVConnRead(adata->avc, acontp, adata->rbuf, uil);
    return 0;
  case INK_EVENT_VCONN_READ_READY:

    INKDebug("extauth", "verify_auth: VCONN_READ_READY");

    INKVIOReenable(adata->avio);
    return 0;
  case INK_EVENT_VCONN_READ_COMPLETE:

    INKDebug("extauth", "verify_auth: VCONN_READ_COMPLETE");

    if (INKIOBufferReaderAvail(adata->rreader) == uil) {
      INKIOBufferBlock rb;
      const char *resp;
      char *respstr;
      int avail, i;

      rb = INKIOBufferReaderStart(adata->rreader);
      resp = INKIOBufferBlockReadStart(rb, adata->rreader, &avail);
      if (avail == uil) {
        respstr = (char *) INKmalloc(sizeof(char) * (uil + 1));
        for (i = 0; i < uil; i++) {
          respstr[i] = resp[i];
        }
        respstr[i] = '\0';
        INKIOBufferReaderConsume(adata->rreader, uil);
        INKDebug("extauth", "AuthServer Response - %s", respstr);
        INKfree(respstr);
      }
      INKIOBufferDestroy(adata->rbuf);
      adata->rbuf = NULL;
      adata->rreader = NULL;
      INKVConnClose(adata->avc);
      adata->avc = NULL;
      adata->avio = NULL;

      INKHttpTxnReenable(adata->txn, INK_EVENT_HTTP_CONTINUE);
      destroy_auth(acontp);
      return 0;
    }
    break;
  case INK_EVENT_NET_CONNECT_FAILED:

    INKDebug("extauth", "verify_auth: NET_CONNECT_FAILED");

    adata->act = NULL;
    break;
  case INK_EVENT_ERROR:

    INKDebug("extauth", "verify_auth: ERROR");

    break;
  default:
    break;
  }

  INKHttpTxnHookAdd(adata->txn, INK_HTTP_SEND_RESPONSE_HDR_HOOK, adata->cont);
  INKHttpTxnReenable(adata->txn, INK_EVENT_HTTP_ERROR);

  destroy_auth(acontp);
  return 0;
}

static void
destroy_auth(INKCont acontp)
{

  AuthData *adata;

  adata = INKContDataGet(acontp);

  if (adata->abuf) {
    INKIOBufferDestroy(adata->abuf);
  }
  if (adata->rbuf) {
    INKIOBufferDestroy(adata->rbuf);
  }
  if (adata->act) {
    INKActionCancel(adata->act);
  }
  if (adata->avc) {
    INKVConnAbort(adata->avc, 1);
  }

  INKfree(adata);
  INKContDestroy(acontp);
  return;
}

static void
require_auth(INKHttpTxn txnp)
{

  INKMBuffer bufp;
  INKMLoc hdr_loc, newfld_loc;

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("require_auth: failed to retrieve client response");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  INKHttpHdrReasonSet(bufp, hdr_loc, INKHttpHdrReasonLookup(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED), -1);

  newfld_loc = INKMimeFieldCreate(bufp);
  INKMimeFieldNameSet(bufp, newfld_loc, INK_MIME_FIELD_PROXY_AUTHENTICATE, -1);
  INKMimeFieldValueInsert(bufp, newfld_loc, "Basic realm=\"Armageddon\"", -1, -1);
  INKMimeHdrFieldInsert(bufp, hdr_loc, newfld_loc, -1);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}
