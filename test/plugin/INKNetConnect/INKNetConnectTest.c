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

static const char *g_strPluginName = "INKNetConnectTest plugin";
static unsigned int g_ConnectionAddress;
static int g_ConnectionPort = 0;

typedef struct
{
  INKCont m_MainCont;
  INKCont m_CheckCont;
  INKVConn m_VConn;
  INKHttpTxn m_Txnp;
  INKMBuffer m_Buffer;
  INKMLoc m_ClientHeaderLoc;
  INKMLoc m_ClientURLLoc;
  INKIOBuffer m_ReadIOBuffer;
  INKIOBuffer m_SendIOBuffer;
  INKAction m_Action;
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
  CheckData_T *pCheckData = (CheckData_T *) INKmalloc(sizeof(CheckData_T));
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
      INKfree(pCheckData->m_strClientURL);
    }

    /* free IOBuffer... */
    if (pCheckData->m_ReadIOBuffer) {
      INKIOBufferDestroy(pCheckData->m_ReadIOBuffer);
    }
    if (pCheckData->m_SendIOBuffer) {
      INKIOBufferDestroy(pCheckData->m_SendIOBuffer);
    }

    if (pCheckData->m_Action) {
      INKActionCancel(pCheckData->m_Action);
    }

    if (pCheckData->m_VConn) {
      INKVConnClose(pCheckData->m_VConn);
    }

    if (pCheckData->m_CheckCont) {
      INKContDestroy(pCheckData->m_CheckCont);
    }

    INKfree(pCheckData);
    pCheckData = NULL;
  }
}

static void
SendCheck(INKCont contp, CheckData_T * pCheckData)
{
  char *strRequestData = (char *) INKmalloc(4096);
  int RequestLength = 0;

  strcpy(strRequestData, "INKNetConnect CHECK");

  RequestLength = strlen(strRequestData);
  pCheckData->m_SendIOBuffer = INKIOBufferCreate();
  INKIOBufferAppend(pCheckData->m_SendIOBuffer,
                    INKIOBufferBlockCreate(INKIOBufferDataCreate(strRequestData, RequestLength, INK_DATA_MALLOCED),
                                           RequestLength, 0));

  INKVConnWrite(pCheckData->m_VConn, contp, INKIOBufferReaderAlloc(pCheckData->m_SendIOBuffer), RequestLength);

  pCheckData->m_ReadIOBuffer = INKIOBufferCreate();
  INKVConnRead(pCheckData->m_VConn, contp, pCheckData->m_ReadIOBuffer, 4096);
}

static void
ReadCheck(INKCont contp, CheckData_T * pCheckData)
{
  INKIOBufferReader IOBufferReader = INKIOBufferReaderAlloc(pCheckData->m_ReadIOBuffer);
  INKIOBufferBlock IOBufferBlock = INKIOBufferReaderStart(IOBufferReader);
  const char *strBuffer = NULL;
  int Avail;

  if (INK_HTTP_TYPE_REQUEST != INKHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc)) {
    /* If the function returns something else, the transaction was canceled. 
       Therefore don't do anything...simply exit... */
    Log("HTTP header was not a INK_HTTP_TYPE_REQUEST (in ReadCheck)");
    /*fwrite (pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc, 1,stderr); */
    fprintf(stderr, " TYPE = %d \n", (int) INKHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc));
  } else {
    if (IOBufferBlock) {
      /* get a pointer to the data... */
      strBuffer = INKIOBufferBlockReadStart(IOBufferBlock, IOBufferReader, &Avail);

      if (Avail) {
        if (strncmp("INKNetConnect CHECK", strBuffer, Avail) == 0) {
          Log("Succeeded");
        } else {
          Log("Failed");
        }
        /* indicate consumption of data... */
        INKIOBufferReaderConsume(IOBufferReader, Avail);
      } else {
        Log("Avail was zero!!!");
      }
    }
  }

  INKHttpTxnReenable(pCheckData->m_Txnp, INK_EVENT_HTTP_CONTINUE);
  INKVConnShutdown(pCheckData->m_VConn, 1, 0);
  CheckDataDestroy(pCheckData);
}

static int
CheckAccessHandler(INKCont contp, INKEvent event, void *edata)
{
  CheckData_T *pCheckData = (CheckData_T *) INKContDataGet(contp);

  switch (event) {
  case INK_EVENT_NET_CONNECT:  /* Connection established */
    Log("INK_EVENT_NET_CONNECT");
    pCheckData->m_VConn = (INKVConn) edata;
    SendCheck(contp, pCheckData);
    break;

  case INK_EVENT_NET_CONNECT_FAILED:   /* Connection failed */
    Log("INK_EVENT_NET_CONNECT_FAILED");
    CheckDataDestroy(pCheckData);
    break;

  case INK_EVENT_VCONN_WRITE_READY:    /* VConnection is ready for writing */
    Log("INK_EVENT_VCONN_WRITE_READY");
    break;

  case INK_EVENT_VCONN_WRITE_COMPLETE: /* VConnection has done its writing */
    Log("INK_EVENT_VCONN_WRITE_COMPLETE");
    INKVConnShutdown(pCheckData->m_VConn, 0, 1);
    break;

  case INK_EVENT_VCONN_READ_READY:     /* VConnection is ready for reading */
    Log("INK_EVENT_VCONN_READ_READY");
    ReadCheck(contp, pCheckData);
    break;

  case INK_EVENT_VCONN_READ_COMPLETE:  /* VConnection has read all data */
    Log("INK_EVENT_VCONN_READ_COMPLETE");
    break;

  case INK_EVENT_VCONN_EOS:
    Log("INK_EVENT_VCONN_EOS");
    CheckDataDestroy(pCheckData);
    break;

  case INK_EVENT_ERROR:
    Log("INK_EVENT_ERROR");
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
HandleRequest(INKHttpTxn txnp, INKCont contp)
{
  INKAction Action;
  const char *ClientURLScheme = NULL;
  CheckData_T *pCheckData = CheckDataCreate();
  pCheckData->m_CheckCont = INKContCreate(CheckAccessHandler, INKMutexCreate());
  pCheckData->m_Txnp = txnp;
  pCheckData->m_MainCont = contp;

  if (!INKHttpTxnClientReqGet(pCheckData->m_Txnp, &pCheckData->m_Buffer, &pCheckData->m_ClientHeaderLoc)) {
    Log("couldn't retrieve client request header!\n");
    goto done;
  }

  if (INK_HTTP_TYPE_REQUEST != INKHttpHdrTypeGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc)) {
    /* If the function returns something else, the transaction was canceled.
       Therefore don't do anything...simply exit... */
    Log("HTTP header was not a INK_HTTP_TYPE_REQUEST (in HandleRequest)");
    return;
  }

  pCheckData->m_ClientURLLoc = INKHttpHdrUrlGet(pCheckData->m_Buffer, pCheckData->m_ClientHeaderLoc);
  if (!pCheckData->m_ClientURLLoc) {
    Log("couldn't retrieve request url!\n");
    goto done;
  }

  /* check if the request-scheme is HTTP */
  ClientURLScheme = INKUrlSchemeGet(pCheckData->m_Buffer, pCheckData->m_ClientURLLoc, NULL);
  if (!ClientURLScheme) {
    Log("couldn't retrieve request url scheme!\n");
    goto done;
  }
  if (strcmp(ClientURLScheme, INK_URL_SCHEME_HTTP) != 0) {
    /* it's not a HTTP request... */
    goto done;
  }


  /* get client-ip */
  pCheckData->m_ClientIP = INKHttpTxnClientIPGet(pCheckData->m_Txnp);

  /* get client-url */
  pCheckData->m_strClientURL = INKUrlStringGet(pCheckData->m_Buffer, pCheckData->m_ClientURLLoc, NULL);
  if (!pCheckData->m_strClientURL) {
    Log("couldn't retrieve request url string!\n");
    goto done;
  }

  INKContDataSet(pCheckData->m_CheckCont, pCheckData);

  Action = INKNetConnect(pCheckData->m_CheckCont, g_ConnectionAddress, g_ConnectionPort);

  if (!INKActionDone(Action)) {
    pCheckData->m_Action = Action;
  }

  return;

done:
  CheckDataDestroy(pCheckData);
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
MediaACEPlugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = NULL;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:        /* after reading the request... */
    txnp = (INKHttpTxn) edata;
    printf("Transaction:%x\n", txnp);
    HandleRequest(txnp, contp);
    break;

  default:
    break;
  }
  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{
  /* Localhost */
  g_ConnectionAddress = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  /* Echo Port */
  g_ConnectionPort = 7;

  if (g_ConnectionAddress > 0) {
    INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(MediaACEPlugin, NULL));
  }
  Log("Loaded");
}
