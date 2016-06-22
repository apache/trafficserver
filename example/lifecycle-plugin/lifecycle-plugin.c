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

/* lifecycle-plugin.c: an example plugin to demonstrate the lifecycle hooks.
 *                    of response body content
 */

#include <stdio.h>
#include <unistd.h>
#include <ts/ts.h>

int
CallbackHandler(TSCont this, TSEvent id, void *no_data)
{
  (void)this;
  (void)no_data;
  switch (id) {
  case TS_EVENT_LIFECYCLE_PORTS_INITIALIZED:
    TSDebug("lifecycle-plugin", "Proxy ports initialized");
    break;
  case TS_EVENT_LIFECYCLE_PORTS_READY:
    TSDebug("lifecycle-plugin", "Proxy ports active");
    break;
  case TS_EVENT_LIFECYCLE_CACHE_READY:
    TSDebug("lifecycle-plugin", "Cache ready");
    break;
  default:
    TSDebug("lifecycle-plugin", "Unexpected event %d", id);
    break;
  }
  return TS_EVENT_NONE;
}

int
CheckVersion()
{
  const char *ts_version = TSTrafficServerVersionGet();
  int result             = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 3.3.5 */
    if (major_ts_version > 3 ||
        (major_ts_version == 3 && (minor_ts_version > 3 || (minor_ts_version == 3 && patch_ts_version >= 5)))) {
      result = 1;
    }
  }
  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont cb;

  (void)argc;
  (void)argv;

  info.plugin_name   = "lifecycle-plugin";
  info.vendor_name   = "My Company";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[lifecycle-plugin] Plugin registration failed.");

    goto Lerror;
  }

  if (!CheckVersion()) {
    TSError("[lifecycle-plugin] Plugin requires Traffic Server 3.3.5 "
            "or later");
    goto Lerror;
  }

  cb = TSContCreate(CallbackHandler, NULL);

  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_READY_HOOK, cb);
  TSLifecycleHookAdd(TS_LIFECYCLE_CACHE_READY_HOOK, cb);

  TSDebug("lifecycle-plugin", "online");

  return;

Lerror:
  TSError("[lifecycle-plugin] Unable to initialize plugin (disabled).");
}
