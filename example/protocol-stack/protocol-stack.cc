/** @file

  an example protocol-stack plugin

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

#include "ts/ts.h"
#include "ts/ink_defs.h"

#define DEBUG_TAG "protocol-stack"

static int
proto_stack_cb(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  char const *results[10];
  int count = 0;
  TSDebug(DEBUG_TAG, "Protocols:");
  TSHttpTxnClientProtocolStackGet(txnp, 10, results, &count);
  for (int i = 0; i < count; i++) {
    TSDebug(DEBUG_TAG, "\t%d: %s", i, results[i]);
  }
  const char *ret_tag = TSHttpTxnClientProtocolStackContains(txnp, "h2");
  TSDebug(DEBUG_TAG, "Stack %s HTTP/2", ret_tag != NULL ? "contains" : "does not contain");
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "protocol-stack";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[protocol-stack] Plugin registration failed.");
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(proto_stack_cb, NULL));
}
