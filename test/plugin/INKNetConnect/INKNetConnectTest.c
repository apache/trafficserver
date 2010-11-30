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
#include <stdio.h>
#include <string.h>
#include "ts.h"

static const char *g_strPluginName = "TSNetConnectTest plugin";
static unsigned int g_ConnectionAddress;
static int g_ConnectionPort = 0;

typedef struct
{
  TSCont m_MainCont;
  TSCont m_CheckCont;
  TSVConn m_VConn;
  TSHttpTxn m_Txnp;
  TSMBuffer m_Buffer;
  TSMLoc m_ClientHeaderLoc;
  TSMLoc m_ClientURLLoc;
  TSIOBuffer m_ReadIOBuffer;
  TSIOBuffer m_SendIOBuffer;
  TSAction m_Action;
  unsigned int m_ClientIP;
  char *m_strClientURL;
} CheckData_T;

static void
Log(const char *strLogMessage)
{
  printf(" %s: %s.\n", g_strPluginName, strLogMessage);
}

static CheckData_T *
CheckDataCreate()
{
  CheckData_T *pCheckData = (CheckData_T *) TSmalloc(sizeof(CheckData_T));
  pCheckData->m_Action = NULL;
  pCheckData->m_Buffer = NULL;
  pCheckData->m_CheckCont = NULL;
  pCheckData->m_ClientHeaderLoc = 0;
  pCheckData->m_ClientIP = 0;
  pCheckData->m_ClientURLLoc = 0;
  pCheckData->m_MainCont = NULL;
  pCheckData->m_ReadIOBuffer = NULL;
  pCheckData->m_SendIOBuffer = NULL;
  pCheckData->m_strClientURL = NULL;
  pCheckData->m_Txnp = NULL;
  pCheckData->m_VConn = NULL;

  return pCheckData;
}

static void
CheckDataDestroy(CheckData_T * pCheckData)
{
  if (pCheckData) {
    /* free strings... */
    if (pCheckData->m_strClientURL) {
      TSfree(pCheckData->m_strClientURL);
    }

    /* free IOBuffer... */
    if (pCheckData->m_ReadIOBuffer) {
      TSIOBufferDestroy(pCheckData->m_ReadIOBuffer);
    }
    if (pCheckData->m_SendIOBuffer) {
      TSIOBufferDestroy(pCheckData->m_SendIOBuffer);
    }

    if (pCheckData->m_Action) {
      TSActionCancel(pCheckData->m_Action);
    }

    if (pCheckData->m_VConn) {
      TSVConnClose(pCheckData->m_VConn);
    }

    if (pCheckData->m_CheckCont) {
      TSContDestroy(pCheckData->m_CheckCont);
    }

    TSfree(pCheckData);
    pCheckData = NULL;
  }
}

static void
SendCheck(TSCont contp, CheckData_T * pCheckData)
{
  char *strRequestData = (char *) TSmalloc(4096);
  int RequestLength = 0;

  strcpy(strRequestData, "TSNetConnect CHECK");

  RequestLength = strlen(strRequestData);
  pCheckData->m_SendIOBuffer = TSIOBufferCreate();
  TSIOBufferAppend(pCheckData->m_SendIOBuffer,
                    TSIOBufferBlockCreate(TSIOBufferDataCreate(strRequestData, RequestLength, TS_DATA_MALLOCED),
                                           RequestLength, 0));

  TSVConnWrite(pCheckData->m_VConn, contp, TSIOBufferReaderAlloc(pCheckData->m_SendIOBuffer), RequestLength);

  pCheckData->m_ReadIOBuffer = TSIOBufferCreate();
  TSVConnRead(pCheckData->m_VConn, contp, pCheckData->m_ReadIOBuffer, 4096);
}

static void
ReadCheck(TSCont contp, CheckData_T * pCheckData)
{
  TSIOBufferReader IOBufferReader = TSIOBufferReaderAlloc(pCheckData->m_ReadIOBuffer);
  TSIOBufferBlock IOBufferBlock = TSIOBufferReaderStart(IOBufferReader);
  const char *strBuffer = NULL;
  int Avail;

  if (TS_HTTP_TYPE_REQUEST != TSHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc)) {
    /* If the function returns something else, the transaction was canceled.
       Therefore don't do anything...simply exit... */
    Log("HTTP header was not a TS_HTTP_TYPE_REQUEST (in ReadCheck)");
    /*fwrite (pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc, 1,stderr); */
    fprintf(stderr, " TYPE = %d \n", (int) TSHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc));
  } else {
    if (IOBufferBlock) {
      /* get a pointer to the data... */
      strBuffer = TSIOBufferBlockReadStart(IOBufferBlock, IOBufferReader, &Avail);

      if (Avail) {
        if (strncmp("TSNetConnect CHECK", strBuffer, Avail) == 0) {
          Log("Succeeded");
        } else {
          Log("Failed");
        }
        /* indicate consumption of data... */
        TSIOBufferReaderConsume(IOBufferReader, Avail);
      } else {
        Log("Avail was zero!!!");
      }
    }
  }

  TSHttpTxnReenable(pCheckData->m_Txnp, TS_EVENT_HTTP_CONTINUE);
  TSVConnShutdown(pCheckData->m_VConn, 1, 0);
  CheckDataDestroy(pCheckData);
}

static int
CheckAccessHandler(TSCont contp, TSEvent event, void *edata)
{
  CheckData_T *pCheckData = (CheckData_T *) TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_NET_CONNECT:  /* Connection established */
    Log("TS_EVENT_NET_CONNECT");
    pCheckData->m_VConn = (TSVConn) edata;
    SendCheck(contp, pCheckData);
    break;

  case TS_EVENT_NET_CONNECT_FAILED:   /* Connection failed */
    Log("TS_EVENT_NET_CONNECT_FAILED");
    CheckDataDestroy(pCheckData);
    break;

  case TS_EVENT_VCONN_WRITE_READY:    /* VConnection is ready for writing */
    Log("TS_EVENT_VCONN_WRITE_READY");
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE: /* VConnection has done its writing */
    Log("TS_EVENT_VCONN_WRITE_COMPLETE");
    TSVConnShutdown(pCheckData->m_VConn, 0, 1);
    break;

  case TS_EVENT_VCONN_READ_READY:     /* VConnection is ready for reading */
    Log("TS_EVENT_VCONN_READ_READY");
    ReadCheck(contp, pCheckData);
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:  /* VConnection has read all data */
    Log("TS_EVENT_VCONN_READ_COMPLETE");
    break;

  case TS_EVENT_VCONN_EOS:
    Log("TS_EVENT_VCONN_EOS");
    CheckDataDestroy(pCheckData);
    break;

  case TS_EVENT_ERROR:
    Log("TS_EVENT_ERROR");
    CheckDataDestroy(pCheckData);
    break;

  default:
    Log("Default");
    CheckDataDestroy(pCheckData);
    break;
  }

  return 0;
}


static void
HandleRequest(TSHttpTxn txnp, TSCont contp)
{
  TSAction Action;
  const char *ClientURLScheme = NULL;
  CheckData_T *pCheckData = CheckDataCreate();
  pCheckData->m_CheckCont = TSContCreate(CheckAccessHandler, TSMutexCreate());
  pCheckData->m_Txnp = txnp;
  pCheckData->m_MainCont = contp;

  if (!TSHttpTxnClientReqGet(pCheckData->m_Txnp, &pCheckData->m_Buffer, &pCheckData->m_ClientHeaderLoc)) {
    Log("couldn't retrieve client request header!\n");
    goto done;
  }

  if (TS_HTTP_TYPE_REQUEST != TSHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc)) {
    /* If the function returns something else, the transaction was canceled.
       Therefore don't do anything...simply exit... */
    Log("HTTP header was not a TS_HTTP_TYPE_REQUEST (in HandleRequest)");
    return;
  }

  pCheckData->m_ClientURLLoc = TSHttpHdrUrlGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc);
  if (!pCheckData->m_ClientURLLoc) {
    Log("couldn't retrieve request url!\n");
    goto done;
  }

  /* check if the request-scheme is HTTP */
  ClientURLScheme = TSUrlSchemeGet(pCheckData->m_Buffer, pCheckData->m_ClientURLLoc, NULL);
  if (!ClientURLScheme) {
    Log("couldn't retrieve request url scheme!\n");
    goto done;
  }
  if (strcmp(ClientURLScheme, TS_URL_SCHEME_HTTP) != 0) {
    /* it's not a HTTP request... */
    goto done;
  }


  /* get client-ip */
  pCheckData->m_ClientIP = TSHttpTxnClientIPGet(pCheckData->m_Txnp);

  /* get client-url */
  pCheckData->m_strClientURL = TSUrlStringGet(pCheckData->m_Buffer, pCheckData->m_ClientURLLoc, NULL);
  if (!pCheckData->m_strClientURL) {
    Log("couldn't retrieve request url string!\n");
    goto done;
  }

  TSContDataSet(pCheckData->m_CheckCont, pCheckData);

  Action = TSNetConnect(pCheckData->m_CheckCont, g_ConnectionAddress, g_ConnectionPort);

  if (!TSActionDone(Action)) {
    pCheckData->m_Action = Action;
  }

  return;

done:
  CheckDataDestroy(pCheckData);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
MediaACEPlugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = NULL;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:        /* after reading the request... */
    txnp = (TSHttpTxn) edata;
    printf("Transaction:%x\n", txnp);
    HandleRequest(txnp, contp);
    break;

  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  /* Localhost */
  g_ConnectionAddress = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  /* Echo Port */
  g_ConnectionPort = 7;

  if (g_ConnectionAddress > 0) {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(MediaACEPlugin, NULL));
  }
  Log("Loaded");
}
