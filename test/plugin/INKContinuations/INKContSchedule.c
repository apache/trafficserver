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


/**************************************************************************
Verification of TSqa06643

Schedule a continuation that is simply called back with a later timeout
value. Explicitly call TSContSchedule() without a mutex, the mutex should
be created in InkAPI.cc/TSContSchedule.

This plug-in will not complete the client request (request times-out),
since the event routine calls TSContSchedule() in the event handler.
A simple change to the event routine can be made so that TSHttpTxnReenable()
is called in place of TSContSchedule().

Entry points to the core now use either
	FORCE_PLUGIN_MUTEX
or
	new_ProxyMutex()

to create/init a mutex.

**************************************************************************/


#include <sys/types.h>
#include <time.h>

#include <ts/ts.h>

/* Verification code for: TSqa06643 */
static int
EventHandler(TSCont contp, TSEvent event, void *eData)
{
  TSHttpTxn txn = (TSHttpTxn) eData;
  int iVal;
  time_t tVal;

  if (time(&tVal) != (time_t) (-1)) {
    TSDebug("tag_sched6643", "TSContSchedule: EventHandler: called at %s\n", ctime(&tVal));
  }

  iVal = (int) TSContDataGet(contp);

  TSDebug("tag_sched6643", "TSContSchedule: handler called with value %d\n", iVal);

  switch (event) {

  case TS_EVENT_HTTP_OS_DNS:
    TSDebug("tag_sched6643", "TSContSchedule: Seed event %s\n", "TS_EVENT_HTTP_OS_DNS");
    break;

  case TS_EVENT_TIMEOUT:
    TSDebug("tag_sched6643", "TSContSchedule: TIMEOUT event\n");
    break;

  default:
    TSDebug("tag_sched6643", "TSContSchedule: Error: default event\n");
    break;
  }

  iVal += 100;                  /* seed + timeout val */
  TSContDataSet(contp, (void *) iVal);
  TSContSchedule(contp, iVal);

  /* TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE); */
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp, contp2;
  int timeOut = 10;

  TSDebug("tag_sched6643", "TSContSchedule: Initial data value for contp is %d\n", timeOut);

  /* contp = TSContCreate(EventHandler, TSMutexCreate() ); */

  contp = TSContCreate(EventHandler, NULL);
  TSContDataSet(contp, (void *) timeOut);

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, contp);
}
