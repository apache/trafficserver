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

#include <cstdio>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "protocol_stack"

static int
proto_stack_cb(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  const char *results[10];
  int count = 0;
  TSDebug(PLUGIN_NAME, "Protocols:");
  TSHttpTxnClientProtocolStackGet(txnp, 10, results, &count);
  for (int i = 0; i < count; i++) {
    TSDebug(PLUGIN_NAME, "\t%d: %s", i, results[i]);
  }
  const char *ret_tag = TSHttpTxnClientProtocolStackContains(txnp, "h2");
  TSDebug(PLUGIN_NAME, "Stack %s HTTP/2", ret_tag != nullptr ? "contains" : "does not contain");
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
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

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(proto_stack_cb, nullptr));
}
