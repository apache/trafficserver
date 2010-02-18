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
Tests for registering/processing:

        INK_HTTP_SESSION_START
        INK_HTTP_TXN_START
        INK_HTTP_SESSION_CLOSE
        INK_HTTP_TXN_CLOSE
*/


#include "ts.h"
#include <sys/types.h>
#include <stdio.h>

const char *const INKEventStrId[] = {
  "INK_EVENT_HTTP_CONTINUE",    /* 60000 */
  "INK_EVENT_HTTP_ERROR",       /* 60001 */
  "INK_EVENT_HTTP_READ_REQUEST_HDR",    /* 60002 */
  "INK_EVENT_HTTP_OS_DNS",      /* 60003 */
  "INK_EVENT_HTTP_SEND_REQUEST_HDR",    /* 60004 */
  "INK_EVENT_HTTP_READ_CACHE_HDR",      /* 60005 */
  "INK_EVENT_HTTP_READ_RESPONSE_HDR",   /* 60006 */
  "INK_EVENT_HTTP_SEND_RESPONSE_HDR",   /* 60007 */
  "INK_EVENT_HTTP_REQUEST_TRANSFORM",   /* 60008 */
  "INK_EVENT_HTTP_RESPONSE_TRANSFORM",  /* 60009 */
  "INK_EVENT_HTTP_SELECT_ALT",  /* 60010 */
  "INK_EVENT_HTTP_TXN_START",   /* 60011 */
  "INK_EVENT_HTTP_TXN_CLOSE",   /* 60012 */
  "INK_EVENT_HTTP_SSN_START",   /* 60013 */
  "INK_EVENT_HTTP_SSN_CLOSE",   /* 60014 */

  "INK_EVENT_MGMT_UPDATE"       /* 60100 */
};

/* 
 * We track that each hook was called using this array. We start with
 * all values set to zero, meaning that the INKEvent has not been 
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
  /* INKDebug("INKHttpHook",  */
  printf("ChkEvents: -- %s -- \n", INKEventStrId[index(event)]);

  for (i = 0; i < inktHookTblSize; i++) {
    if (!inktHookTbl[i]) {
      printf("Event [%d] %s registered and not called back\n", i, INKEventStrId[i]);
      re = 1;
    }
  }
  return re;
}


/* event routine: for each INKHttpHookID this routine should be called 
 * with a matching event. 
*/
static int
INKHttpHook(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_START);

    /*
     * We do have a transaction. 
     * Probably, both of these will activate this event. This
     * is an implementation detail: where do you want the hook to
     * live, session or transaction ? Should be transparent.
     */
    /* OK */
    INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, contp);

    /* Event lives in the session. Transaction is deleted before
     * the session. Event will not be received
     * INKHttpSsnHookAdd(ssnp,INK_HTTP_TXN_CLOSE_HOOK,contp);
     */

    /* Since this is a transaction level event, activate the
     * transaction.
     */
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_CLOSE)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_CLOSE);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SSN_START:
    /* Reged at the "session" level, all but 
     * INK_HTTP_TXN_CLOSE_HOOK is received. 
     */
    inktHookTbl[index(INK_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_SSN_START);

    /* There has to be some way to get from the session to
     * the transaction. This is how:
     * No transaction yet, register TXN_START with the session 
     */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);

    /* session level event with the session */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SSN_CLOSE_HOOK, contp);
#if 0
    /* INK_HTTP_TXN_CLOSE_HOOK will not be delivered if registered 
     *  here since the hook id lives in the session and the 
     *  transaction is deleted before the session.  Register the
     *  the event at TXN_START, event will live in the txn.
     */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_CLOSE_HOOK, contp);
#endif
    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SSN_CLOSE:
    /* Here as a result of: 
     * INKHTTPHookAdd(INK_HTTP_SSN_CLOSE_HOOK) 
     */
    inktHookTbl[index(INK_EVENT_HTTP_SSN_CLOSE)] = 1;

    /* Assumption: at this point all other events have
     * have been called. Since a session can have one or
     * more transactions, the close of a session should
     * prompt us to check that all events have been called back 
     * CAUTION: can a single request trigger all events? 
     */
    if (ChkEvents(INK_EVENT_HTTP_SSN_CLOSE))
      INKError("INKHttpHook: Fail: All events not called back.\n");
    else
      INKError("INKHttpHook: Pass: All events called back.\n");

    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    INKError("INKHttpHook: undefined event [%d] received\n", event);
    break;
  }
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont myCont = NULL;
  inktHookTblSize = sizeof(inktHookTbl) / sizeof(int);

  /* Create continuation */
  myCont = INKContCreate(INKHttpHook, NULL);
  if (myCont != NULL) {

    /* Reged at the "global" level, these 4 events are
     * received. 
     */
    INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, myCont);
  } else
    INKError("INKHttpHook: INKContCreate() failed \n");
}
