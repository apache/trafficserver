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

/* lifecycle_plugin.c: an example plugin to demonstrate the lifecycle hooks.
 *                    of response body content
 */

#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <ts/ts.h>

#define PLUGIN_NAME "lifecycle"

int
CallbackHandler(TSCont this, TSEvent id, void *data)
{
  (void)this; // make compiler shut up about unused variable.
  switch (id) {
  case TS_EVENT_LIFECYCLE_PORTS_INITIALIZED:
    TSDebug(PLUGIN_NAME, "Proxy ports initialized");
    break;
  case TS_EVENT_LIFECYCLE_PORTS_READY:
    TSDebug(PLUGIN_NAME, "Proxy ports active");
    break;
  case TS_EVENT_LIFECYCLE_CACHE_READY:
    TSDebug(PLUGIN_NAME, "Cache ready");
    break;
  case TS_EVENT_LIFECYCLE_MSG: {
    TSPluginMsg *msg = (TSPluginMsg *)data;
    TSDebug(PLUGIN_NAME, "Message to '%s' - %zu bytes of data", msg->tag, msg->data_size);
    if (msg->data_size == 0) {
      TSDebug(PLUGIN_NAME, "Message data is not available");
    }

    break;
  }
  default:
    TSDebug(PLUGIN_NAME, "Unexpected event %d", id);
    break;
  }
  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont cb;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);

    goto Lerror;
  }

  cb = TSContCreate(CallbackHandler, NULL);

  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_CACHE_READY_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, cb);

  TSDebug(PLUGIN_NAME, "online");

  return;

Lerror:
  TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
}
