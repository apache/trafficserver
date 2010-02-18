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


#if 0
/************************************************************************** 
 * HTTP sessions 
 *************************************************************************/

/* HTTP hooks */
inkapi void INKHttpHookAdd(INKHttpHookID id, INKCont contp);
typedef enum
{
  INK_HTTP_READ_REQUEST_HDR_HOOK,
  INK_HTTP_OS_DNS_HOOK,
  INK_HTTP_SEND_REQUEST_HDR_HOOK,
  INK_HTTP_READ_CACHE_HDR_HOOK,
  INK_HTTP_READ_RESPONSE_HDR_HOOK,
  INK_HTTP_SEND_RESPONSE_HDR_HOOK,
  INK_HTTP_REQUEST_TRANSFORM_HOOK,
  INK_HTTP_RESPONSE_TRANSFORM_HOOK,
  INK_HTTP_SELECT_ALT_HOOK,
  INK_HTTP_TXN_START_HOOK,
  INK_HTTP_TXN_CLOSE_HOOK,
  INK_HTTP_SSN_START_HOOK,
  INK_HTTP_SSN_CLOSE_HOOK,

  INK_HTTP_LAST_HOOK
} INKHttpHookID;

typedef enum
{
  INK_EVENT_NONE = 0,
  INK_EVENT_IMMEDIATE = 1,
  INK_EVENT_TIMEOUT = 2,
  INK_EVENT_ERROR = 3,
  INK_EVENT_CONTINUE = 4,
  INK_CACHE_COULD_NOT_FIND = 5,
  INK_CACHE_LOOKUP_COMPLETE = 6,
  INK_CACHE_TIMEOUT_ERROR = 7,

  INK_EVENT_VCONN_READ_READY = 100,
  INK_EVENT_VCONN_WRITE_READY = 101,
  INK_EVENT_VCONN_READ_COMPLETE = 102,
  INK_EVENT_VCONN_WRITE_COMPLETE = 103,
  INK_EVENT_VCONN_EOS = 104,
  INK_EVENT_VCONN_INACTIVITY_TIMEOUT = 105,

  INK_EVENT_NET_CONNECT = 200,
  INK_EVENT_NET_CONNECT_FAILED = 201,
  INK_EVENT_NET_ACCEPT = 202,
  INK_EVENT_NET_ACCEPT_FAILED = 204,

  INK_EVENT_DNS_LOOKUP = 500,

  INK_EVENT_CACHE_OPEN_READ = 1102,
  INK_EVENT_CACHE_OPEN_READ_FAILED = 1103,
  INK_EVENT_CACHE_OPEN_WRITE = 1108,
  INK_EVENT_CACHE_OPEN_WRITE_FAILED = 1109,
  INK_EVENT_CACHE_REMOVE = 1112,
  INK_EVENT_CACHE_REMOVE_FAILED = 1113,
  INK_EVENT_CACHE_SCAN = 1120,
  INK_EVENT_CACHE_SCAN_FAILED = 1121,
  INK_EVENT_CACHE_SCAN_OBJECT = 1122,
  INK_EVENT_CACHE_SCAN_DONE = 1123,

  /* Used by INKHttpSssnReenable() to cont/term a _session_ */
  /* Used by INKHttpTxnReenable()  to cont/term a _transaction_ */
  INK_EVENT_HTTP_CONTINUE = 60000,
  INK_EVENT_HTTP_ERROR = 60001,


  INK_EVENT_HTTP_READ_REQUEST_HDR = 60002,
  INK_EVENT_HTTP_OS_DNS = 60003,
  INK_EVENT_HTTP_SEND_REQUEST_HDR = 60004,
  INK_EVENT_HTTP_READ_CACHE_HDR = 60005,
  INK_EVENT_HTTP_READ_RESPONSE_HDR = 60006,
  INK_EVENT_HTTP_SEND_RESPONSE_HDR = 60007,
  INK_EVENT_HTTP_REQUEST_TRANSFORM = 60008,
  INK_EVENT_HTTP_RESPONSE_TRANSFORM = 60009,
  INK_EVENT_HTTP_SELECT_ALT = 60010,
  INK_EVENT_HTTP_TXN_START = 60011,
  INK_EVENT_HTTP_TXN_CLOSE = 60012,
  INK_EVENT_HTTP_SSN_START = 60013,
  INK_EVENT_HTTP_SSN_CLOSE = 60014,

  INK_EVENT_MGMT_UPDATE = 60100
} INKEvent;


1. inkapi void INKHttpHookAdd(INKHttpHookID id, INKCont contp);
Called for each of 13 known HookIDs specified in INKHttpHookID.
  2. inkapi void INKHttpSsnHookAdd(INKHttpSsn ssnp, INKHttpHookID id, INKCont contp);

Called for each of 13 known HookIDs specified in INKHttpHookID.
  3. inkapi void INKHttpSsnReenable(INKHttpSsn ssnp, INKEvent event);
INK_EVENT_HTTP_CONTINUE INK_EVENT_HTTP_ERROR HTTP Transactions 4. void
INKHttpTxnReenable(INKHttpTxn txnp, INKEvent INK_EVENT_HTTP_ERROR)
INK_EVENT_HTTP_CONTINUE INK_EVENT_HTTP_ERROR
/************************************************************************** 
 * HTTP sessions 
 *************************************************************************/
#endif
#if 0
typedef enum
{
  INK_HTTP_READ_REQUEST_HDR_HOOK,
  INK_HTTP_OS_DNS_HOOK,
  INK_HTTP_SEND_REQUEST_HDR_HOOK,
  INK_HTTP_READ_CACHE_HDR_HOOK,
  INK_HTTP_READ_RESPONSE_HDR_HOOK,
  INK_HTTP_SEND_RESPONSE_HDR_HOOK,
  INK_HTTP_REQUEST_TRANSFORM_HOOK,
  INK_HTTP_RESPONSE_TRANSFORM_HOOK,
  INK_HTTP_SELECT_ALT_HOOK,
  INK_HTTP_TXN_START_HOOK,
  INK_HTTP_TXN_CLOSE_HOOK,
  INK_HTTP_SSN_START_HOOK,
  INK_HTTP_SSN_CLOSE_HOOK,

  INK_HTTP_LAST_HOOK
} INKHttpHookID;
#endif

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
  INKDebug("INKHttpHook", "ChkEvents: -- %s -- ", INKEventStrId[index(event)]);

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
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_REQUEST_HDR)] = 1;
    /* List what events have been called back at 
     * this point in procesing 
     */
    ChkEvents(INK_EVENT_HTTP_READ_REQUEST_HDR);
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
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_CLOSE)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_CLOSE);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SSN_START:

    inktHookTbl[index(INK_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_SSN_START);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);

#if 0
    /* For this session, call this plug-in back at the start
     * of each txn 
     */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);

    if (ssn_start_firstTime) {
      INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
      ssn_start_firstTime = B_FALSE;
    } else
      INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_ERROR);
#endif
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

    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);

#if 0
    /* For this session, call this plug-in back at the close
     * of each txn 
     */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_CLOSE_HOOK, contp);
    if (ssn_close_firstTime) {
      INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
      ssn_close_firstTime = B_FALSE;
    } else
      INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_ERROR);
#endif
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
    INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_SEND_REQUEST_HDR_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_READ_CACHE_HDR_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, myCont);

    /* These are transactional 
     * INKHttpHookAdd(INK_HTTP_REQUEST_TRANSFORM_HOOK, myCont);
     * INKHttpHookAdd(INK_HTTP_RESPONSE_TRANSFORM_HOOK, myCont);
     */

    INKHttpHookAdd(INK_HTTP_SELECT_ALT_HOOK, myCont);
    /* TODO this is transactional and not global */
    INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_TXN_CLOSE_HOOK, myCont);

    /* INKqa08194:
     * INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, myCont);
     */
    INKHttpHookAdd(INK_HTTP_SSN_CLOSE_HOOK, myCont);
  } else
    INKError("INKHttpHook: INKContCreate() failed \n");
}
