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

/* thread-1.c:  an example program that creates a thread
 *
 *
 *
 *	Usage:
 *	  thread-1.so
 *
 *
 */

#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"

static void *
reenable_txn(void *data)
{
  TSHttpTxn txnp = (TSHttpTxn)data;
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return NULL;
}

static int
thread_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_OS_DNS:
    /**
     * Check if the thread has been created successfully or not.
     * If the thread has not been created successfully, assert.
     */
    if (!TSThreadCreate(reenable_txn, edata)) {
      TSReleaseAssert(!"Failure in thread creation");
    }
    return 0;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "thread-1";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[thread-1] Plugin registration failed.");
  }

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, TSContCreate(thread_plugin, NULL));
}
