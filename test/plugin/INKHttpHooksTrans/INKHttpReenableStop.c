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
TODO tests of stoping an event at the session and transaction level


#include "InkAPI.h"
#include <sys/types.h>
#include <stdio.h>


#if 0
*/
/************************************************************************** 
 * HTTP Sessions 
 *************************************************************************/

/*
1. inkapi void INKHttpHookAdd(INKHttpHookID id, INKCont contp);
   Covered in INKHttpHookAdd.c
2. inkapi void INKHttpSsnHookAdd(INKHttpSsn ssnp, INKHttpHookID id, INKCont contp);
   Called for all events except INK_HTTP_SESSION_START, INK_EVENT_MGMT_UPDATE.
3. inkapi void INKHttpSsnReenable(INKHttpSsn ssnp, INKEvent event);
   INK_EVENT_HTTP_CONTINUE
TODO build a test case for event INK_EVENT_HTTP_ERROR HTTP Transactions
4. void INKHttpTxnReenable(INKHttpTxn txnp, INKEvent INK_EVENT_HTTP_ERROR)
   INK_EVENT_HTTP_CONTINUE
TODO build a test case for event INK_EVENT_HTTP_ERROR
*/

/************************************************************************** 
 * HTTP sessions 
 *************************************************************************/
#endif
const char *const INKEventStrId[]
  = {
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

/* Since this is event based, it can be re-used with 
 * INKHttpHookAdd()
 * INKHttpSsnHookAdd()
 * INKHttpTxnHokkAdd()
*/
static int
ChkEvents(const int event)
{
  int i, re = 0;
  INKDebug("INKHttpReenableStop", "ChkEvents: -- %s -- ", INKEventStrId[index(event)]);

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
SsnHookAddEvent(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_REQUEST_HDR)] = 1;
    /* List what events have been called back at 
     * this point in procesing 
     */
    ChkEvents(INK_EVENT_HTTP_READ_REQUEST_HDR);

    /* This event is delivered to a transaction. Reenable the
     * txnp pointer not the session. 
     */
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_OS_DNS:
    inktHookTbl[index(INK_EVENT_HTTP_OS_DNS)] = 1;
    ChkEvents(INK_EVENT_HTTP_OS_DNS);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_SEND_REQUEST_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_SEND_REQUEST_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_CACHE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_CACHE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_READ_CACHE_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_RESPONSE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_READ_RESPONSE_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_SEND_RESPONSE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_SEND_RESPONSE_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_REQUEST_TRANSFORM:
    inktHookTbl[index(INK_EVENT_HTTP_REQUEST_TRANSFORM)] = 1;
    ChkEvents(INK_EVENT_HTTP_REQUEST_TRANSFORM);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_RESPONSE_TRANSFORM:
    inktHookTbl[index(INK_EVENT_HTTP_RESPONSE_TRANSFORM)] = 1;
    ChkEvents(INK_EVENT_HTTP_RESPONSE_TRANSFORM);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SELECT_ALT:
    inktHookTbl[index(INK_EVENT_HTTP_SELECT_ALT)] = 1;
    ChkEvents(INK_EVENT_HTTP_SELECT_ALT);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_START:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_START);
    /* 
     * Ends the transaction 
     */
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_CLOSE)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_CLOSE);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SSN_START:

    inktHookTbl[index(INK_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_SSN_START);        /* code re-use */

    /* For this session, register for all events */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_REQUEST_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_OS_DNS_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SEND_REQUEST_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_CACHE_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_REQUEST_TRANSFORM_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SELECT_ALT_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_CLOSE_HOOK, contp);


                /******************************************************** 
		* We've already registered for this event as a global 
		* hook. Registering for this event at the session 
		* level will send this event twice: once for the registration
		* done at PluginInit and once for the sessions.
		*
		INKHttpSsnHookAdd(ssnp,INK_HTTP_SSN_START_HOOK, contp);
		*******************************************************/

    INKHttpSsnHookAdd(ssnp, INK_HTTP_SSN_CLOSE_HOOK, contp);

    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);

    break;

  case INK_EVENT_HTTP_SSN_CLOSE:
    /* Here as a result of: 
     * INKHTTPSsnHookAdd(ssnp, INK_HTTP_SSN_CLOSE_HOOK, contp) 
     */
    inktHookTbl[index(INK_EVENT_HTTP_SSN_CLOSE)] = 1;

    /* Assumption: at this point all other events have
     * have been called. Since a session can have one or
     * more transactions, the close of a session should
     * prompt us to check that all events have been called back 
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
  myCont = INKContCreate(SsnHookAddEvent, NULL);
  if (myCont != NULL) {
    /* We need to register ourselves with a global hook
     * so that we can process a session.
     */
    INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, myCont);
  } else
    INKError("INKHttpHook: INKContCreate() failed \n");
}
