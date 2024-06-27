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

#include <cstdio>
#include <unistd.h>
#include <cinttypes>
#include <ts/ts.h>

#define PLUGIN_NAME "lifecycle"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};
}

int
CallbackHandler(TSCont, TSEvent id, void *data)
{
  switch (id) {
  case TS_EVENT_LIFECYCLE_PORTS_INITIALIZED:
    Dbg(dbg_ctl, "Proxy ports initialized");
    break;
  case TS_EVENT_LIFECYCLE_PORTS_READY:
    Dbg(dbg_ctl, "Proxy ports active");
    break;
  case TS_EVENT_LIFECYCLE_CACHE_READY:
    Dbg(dbg_ctl, "Cache ready");
    break;
  case TS_EVENT_LIFECYCLE_MSG: {
    TSPluginMsg *msg = static_cast<TSPluginMsg *>(data);
    Dbg(dbg_ctl, "Message to '%s' - %zu bytes of data", msg->tag, msg->data_size);
    if (msg->data_size == 0) {
      Dbg(dbg_ctl, "Message data is not available");
    }

    break;
  }
  default:
    Dbg(dbg_ctl, "Unexpected event %d", id);
    break;
  }
  return TS_EVENT_NONE;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  TSCont                   cb;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);

    goto Lerror;
  }

  cb = TSContCreate(CallbackHandler, nullptr);

  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_CACHE_READY_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, cb);

  Dbg(dbg_ctl, "online");

  return;

Lerror:
  TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
}
