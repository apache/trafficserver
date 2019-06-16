/** @file

  VConn Args test plugin.

  Tests VConn arg reserve/lookup/set/get.

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

const char *PLUGIN_NAME = "vconn_arg_test";
static int last_arg     = 0;

static int
vconn_arg_handler(TSCont contp, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  switch (event) {
  case TS_EVENT_VCONN_START: {
    // Testing set argument
    int idx = 0;
    while (TSVConnArgIndexReserve(PLUGIN_NAME, "test", &idx) == TS_SUCCESS) {
      char *buf = (char *)TSmalloc(64);
      snprintf(buf, 64, "Test Arg Idx %d", idx);
      TSVConnArgSet(ssl_vc, idx, (void *)buf);
      TSDebug(PLUGIN_NAME, "Successfully reserve and set arg #%d", idx);
    }
    last_arg = idx;
    break;
  }
  case TS_EVENT_SSL_SERVERNAME: {
    // Testing lookup argument
    int idx = 0;
    while (idx <= last_arg) {
      const char *name = nullptr;
      const char *desc = nullptr;
      if (TSVConnArgIndexLookup(idx, &name, &desc) == TS_SUCCESS) {
        TSDebug(PLUGIN_NAME, "Successful lookup for arg #%d: [%s] [%s]", idx, name, desc);
      } else {
        TSDebug(PLUGIN_NAME, "Failed lookup for arg #%d", idx);
      }
      idx++;
    }
    break;
  }
  case TS_EVENT_VCONN_CLOSE: {
    // Testing argget and delete
    int idx = 0;
    while (idx <= last_arg) {
      char *buf = (char *)TSVConnArgGet(ssl_vc, idx);
      if (buf) {
        TSDebug(PLUGIN_NAME, "Successfully retrieve vconn arg #%d: %s", idx, buf);
        TSfree(buf);
      } else {
        TSDebug(PLUGIN_NAME, "Failed to retrieve vconn arg #%d", idx);
      }
      idx++;
    }
  } break;
  default: {
    TSDebug(PLUGIN_NAME, "Unexpected event %d", event);
    break;
  }
  }
  TSVConnReenable(ssl_vc);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "Initializing plugin.");
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Oath";
  info.support_email = "zeyuany@oath.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Unable to initialize plugin. Failed to register.", PLUGIN_NAME);
  } else {
    TSCont cb = TSContCreate(vconn_arg_handler, nullptr);
    TSHttpHookAdd(TS_VCONN_START_HOOK, cb);
    TSHttpHookAdd(TS_SSL_SERVERNAME_HOOK, cb);
    TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, cb);
  }
}
