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
Cannot asynchronously process this event. Same as a non-blocking event,
i.e., event does not have to be reenabled at any level.

Tests that register, receive and process event TS_HTTP_SELECT_ALT_HOOK. This
test was written as a stand-alone plug-in since there appeared to be
interactions with other events that interfered with this event. Once this
code works, it could be incorporated into the TSHttpTxn.c plug-in since this
is a test of "global" hook/event processing.

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
  /* TSDebug("TSHttpHook",  */
  printf("ChkEvents: -- %s -- \n", TSEventStrId[index(event)]);

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
  TSHttpAltInfo pAltInfo = (TSHttpAltInfo) eData;
  float mult = 0.0;

  switch (event) {

  case TS_EVENT_HTTP_SSN_START:

    /* Reged at the "session" level, all but
     * TS_HTTP_TXN_CLOSE_HOOK is received.
     */
    inktHookTbl[index(TS_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(TS_EVENT_HTTP_SSN_START);

    /* Only a global hook/event
     * TSHttpSsnHookAdd(ssnp,TS_HTTP_SELECT_ALT_HOOK,contp);
     */

    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SELECT_ALT:
    inktHookTbl[index(TS_EVENT_HTTP_SELECT_ALT)] = 1;
    ChkEvents(TS_EVENT_HTTP_SELECT_ALT);

    /* Cache hit
     * Now set mult value based on cached req IP address.
     */

    /* Get IP address */

    /* if IP address is X   then mult = 0.001 non-transformed cnt */
    /* if IP address is X'  then mult = 0.009 transformed cnt */

    mult = 0.0123;

    TSHttpAltInfoQualitySet(pAltInfo, mult);
    printf("TSHttpSelAtl: pAltInfo: 0x%0x8,  mult: %f\n", pAltInfo, mult);

    /* Get the cached client req  URL for this pAltInfo/multiplier value */

    /* Wrong:
       /* Get the cached client resp URL for this pAltInfo/multiplier value */
    /* Should be:
       /* Get the cached o.s.   resp URL for this pAltInfo/multiplier value */

    /* Get the        client resp URL for this pAltInfo/multiplier value */

    /* Cannot asynchronously process this event */
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

    /* Reged at the "global" level, these 4 events are
     * received.
     */
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, myCont);
    TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, myCont);

  } else
    TSError("TSHttpHook: TSContCreate() failed \n");
}
