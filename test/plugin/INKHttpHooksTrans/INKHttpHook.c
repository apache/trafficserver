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
Tests for "global" hooks, i.e., registering for events not at the session
or transaction level and processing those events.

TODO TRANSFORM hooks are not "global" but transactional--address this
*/


#include "ts.h"
#include <sys/types.h>
#include <stdio.h>



const char *const TSEventStrId[] = {
  "TS_EVENT_HTTP_CONTINUE",    /* 60000 */
  "TS_EVENT_HTTP_ERROR",       /* 60001 */
  "TS_EVENT_HTTP_READ_REQUEST_HDR",    /* 60002 */
  "TS_EVENT_HTTP_OS_DNS",      /* 60003 */
  "TS_EVENT_HTTP_SEND_REQUEST_HDR",    /* 60004 */
  "TS_EVENT_HTTP_READ_CACHE_HDR",      /* 60005 */
  "TS_EVENT_HTTP_READ_RESPONSE_HDR",   /* 60006 */
  "TS_EVENT_HTTP_SEND_RESPONSE_HDR",   /* 60007 */
  "TS_EVENT_HTTP_REQUEST_TRANSFORM",   /* 60008 */
  "TS_EVENT_HTTP_RESPONSE_TRANSFORM",  /* 60009 */
  "TS_EVENT_HTTP_SELECT_ALT",  /* 60010 */
  "TS_EVENT_HTTP_TXN_START",   /* 60011 */
  "TS_EVENT_HTTP_TXN_CLOSE",   /* 60012 */
  "TS_EVENT_HTTP_SSN_START",   /* 60013 */
  "TS_EVENT_HTTP_SSN_CLOSE",   /* 60014 */

  "TS_EVENT_MGMT_UPDATE"       /* 60100 */
};

/*
 * We track that each hook was called using this array. We start with
 * all values set to zero, meaning that the TSEvent has not been
 * received.
 * There 16 entries.
*/
static int inktHookTbl[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define		index(x)	((x)%(1000))
static int inktHookTblSize;

static int
ChkEvents(const int event)
{
  int i, re = 0;
  TSDebug("TSHttpHook", "ChkEvents: -- %s -- ", TSEventStrId[index(event)]);

  for (i = 0; i < inktHookTblSize; i++) {
    if (!inktHookTbl[i]) {
      printf("Event [%d] %s registered and not called back\n", i, TSEventStrId[i]);
      re = 1;
    }
  }
  return re;
}


/* event routine: for each TSHttpHookID this routine should be called
 * with a matching event.
*/
static int
TSHttpHook(TSCont contp, TSEvent event, void *eData)
{
  TSHttpSsn ssnp = (TSHttpSsn) eData;
  TSHttpTxn txnp = (TSHttpTxn) eData;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    inktHookTbl[index(TS_EVENT_HTTP_READ_REQUEST_HDR)] = 1;
    /* List what events have been called back at
     * this point in procesing
     */
    ChkEvents(TS_EVENT_HTTP_READ_REQUEST_HDR);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_OS_DNS:
    inktHookTbl[index(TS_EVENT_HTTP_OS_DNS)] = 1;
    ChkEvents(TS_EVENT_HTTP_OS_DNS);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    inktHookTbl[index(TS_EVENT_HTTP_SEND_REQUEST_HDR)] = 1;
    ChkEvents(TS_EVENT_HTTP_SEND_REQUEST_HDR);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:
    inktHookTbl[index(TS_EVENT_HTTP_READ_CACHE_HDR)] = 1;
    ChkEvents(TS_EVENT_HTTP_READ_CACHE_HDR);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    inktHookTbl[index(TS_EVENT_HTTP_READ_RESPONSE_HDR)] = 1;
    ChkEvents(TS_EVENT_HTTP_READ_RESPONSE_HDR);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    inktHookTbl[index(TS_EVENT_HTTP_SEND_RESPONSE_HDR)] = 1;
    ChkEvents(TS_EVENT_HTTP_SEND_RESPONSE_HDR);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_REQUEST_TRANSFORM:
    inktHookTbl[index(TS_EVENT_HTTP_REQUEST_TRANSFORM)] = 1;
    ChkEvents(TS_EVENT_HTTP_REQUEST_TRANSFORM);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_RESPONSE_TRANSFORM:
    inktHookTbl[index(TS_EVENT_HTTP_RESPONSE_TRANSFORM)] = 1;
    ChkEvents(TS_EVENT_HTTP_RESPONSE_TRANSFORM);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SELECT_ALT:
    inktHookTbl[index(TS_EVENT_HTTP_SELECT_ALT)] = 1;
    ChkEvents(TS_EVENT_HTTP_SELECT_ALT);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_TXN_START:
    inktHookTbl[index(TS_EVENT_HTTP_TXN_START)] = 1;
    ChkEvents(TS_EVENT_HTTP_TXN_START);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    inktHookTbl[index(TS_EVENT_HTTP_TXN_CLOSE)] = 1;
    ChkEvents(TS_EVENT_HTTP_TXN_CLOSE);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SSN_START:

    inktHookTbl[index(TS_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(TS_EVENT_HTTP_SSN_START);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

    break;

  case TS_EVENT_HTTP_SSN_CLOSE:
    /* Here as a result of:
     * TSHTTPHookAdd(TS_HTTP_SSN_CLOSE_HOOK)
     */
    inktHookTbl[index(TS_EVENT_HTTP_SSN_CLOSE)] = 1;

    /* Assumption: at this point all other events have
     * have been called. Since a session can have one or
     * more transactions, the close of a session should
     * prompt us to check that all events have been called back
     * CAUTION: can a single request trigger all events?
     */
    if (ChkEvents(TS_EVENT_HTTP_SSN_CLOSE))
      TSError("TSHttpHook: Fail: All events not called back.\n");
    else
      TSError("TSHttpHook: Pass: All events called back.\n");

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

    break;

  default:
    TSError("TSHttpHook: undefined event [%d] received\n", event);
    break;
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont myCont = NULL;
  inktHookTblSize = sizeof(inktHookTbl) / sizeof(int);

  /* Create continuation */
  myCont = TSContCreate(TSHttpHook, NULL);
  if (myCont != NULL) {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, myCont);

    /* These are transactional
     * TSHttpHookAdd(TS_HTTP_REQUEST_TRANSFORM_HOOK, myCont);
     * TSHttpHookAdd(TS_HTTP_RESPONSE_TRANSFORM_HOOK, myCont);
     */

    TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, myCont);
    /* TODO this is transactional and not global */
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, myCont);

    /* TSqa08194:
     * TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, myCont);
     */
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, myCont);
  } else
    TSError("TSHttpHook: TSContCreate() failed \n");
}
