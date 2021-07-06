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

/* memory_profile.cc
 *   Responds to plugin messages to dump and activate memory profiling
 *   System must be built with jemalloc to be useful
 */

#include <cstdio>
#include <unistd.h>
#include <cinttypes>
#include <ts/ts.h>
#include <cstring>
#include <cerrno>
#include <tscore/ink_config.h>
#if TS_HAS_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#define PLUGIN_NAME "memory_profile"

int
CallbackHandler(TSCont cont, TSEvent id, void *data)
{
  (void)cont; // make compiler shut up about unused variable.

  if (id == TS_EVENT_LIFECYCLE_MSG) {
    TSPluginMsg *msg = static_cast<TSPluginMsg *>(data);
    TSDebug(PLUGIN_NAME, "Message to '%s' - %zu bytes of data", msg->tag, msg->data_size);
    if (strcmp(PLUGIN_NAME, msg->tag) == 0) { // Message is for us
#if TS_HAS_JEMALLOC
      if (msg->data_size) {
        int retval = 0;
        if (strncmp((char *)msg->data, "dump", msg->data_size) == 0) {
          if ((retval = mallctl("prof.dump", nullptr, nullptr, nullptr, 0)) != 0) {
            TSError("mallct(prof.dump) failed retval=%d errno=%d", retval, errno);
          }
        } else if (strncmp((char *)msg->data, "activate", msg->data_size) == 0) {
          bool active = true;

          if ((retval = mallctl("prof.active", nullptr, nullptr, &active, sizeof(active))) != 0) {
            TSError("mallct(prof.activate) on failed retval=%d errno=%d", retval, errno);
          }
        } else if (strncmp((char *)msg->data, "deactivate", msg->data_size) == 0) {
          bool active = false;
          if ((retval = mallctl("prof.active", nullptr, nullptr, &active, sizeof(active))) != 0) {
            TSError("mallct(prof.activate) off failed retval=%d errno=%d", retval, errno);
          }
        } else if (strncmp((char *)msg->data, "stats", msg->data_size) == 0) {
          malloc_stats_print(nullptr, nullptr, nullptr);
        } else {
          TSError("Unexpected msg %*.s", (int)msg->data_size, (char *)msg->data);
        }
      }
#else
      TSError("Not built with jemalloc");
#endif
    }
  } else {
    TSError("Unexpected event %d", id);
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

  cb = TSContCreate(CallbackHandler, nullptr);

  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, cb);

  TSDebug(PLUGIN_NAME, "online");

  return;

Lerror:
  TSError("[%s] Unable to initialize plugin (disabled)", PLUGIN_NAME);
}
