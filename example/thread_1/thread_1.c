/** @file

  An example plugin that creates a thread.

  The thread is created on the DNS lookup hook and simply re-enables the transaction from the thread.

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

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "thread_1"

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
      TSReleaseAssert(!PLUGIN_NAME " - Failure in thread creation");
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

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, TSContCreate(thread_plugin, NULL));
}
