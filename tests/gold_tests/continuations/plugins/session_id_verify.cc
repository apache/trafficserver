/**
  @file
  @brief A plugin to print session id values.

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
#include <ts/ts.h>    // for debug
#include <inttypes.h> // for PRIu64
#include <unordered_set>

// plugin registration info
#define PLUGIN_NAME "session_id_verify"
static char plugin_name[]   = PLUGIN_NAME;
static char vendor_name[]   = "Apache";
static char support_email[] = "bneradt@apache.org";

int
global_handler(TSCont continuation, TSEvent event, void *data)
{
  TSHttpSsn session = static_cast<TSHttpSsn>(data);

  switch (event) {
  case TS_EVENT_HTTP_SSN_START: {
    TSDebug(PLUGIN_NAME, " -- global_handler :: TS_EVENT_HTTP_SSN_START");
    int64_t id = TSHttpSsnIdGet(session);

    static std::unordered_set<int64_t> seen_ids;
    if (seen_ids.find(id) != seen_ids.end()) {
      TSError("[%s] Plugin encountered a duplicate session id: %ld", PLUGIN_NAME, id);
    } else {
      seen_ids.insert(id);
    }
    TSDebug(PLUGIN_NAME, "session id: %ld", id);
  } break;

  default:
    return 0;
  }

  TSHttpSsnReenable(session, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = plugin_name;
  info.vendor_name   = vendor_name;
  info.support_email = support_email;

#if (TS_VERSION_MAJOR < 3)
  if (TSPluginRegister(TS_SDK_VERSION_2_0, &info) != TS_SUCCESS) {
#elif (TS_VERSION_MAJOR < 6)
  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
#else
  if (TSPluginRegister(&info) != TS_SUCCESS) {
#endif
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
  }

  TSCont contp = TSContCreate(global_handler, TSMutexCreate());
  if (contp == nullptr) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s] could not create continuation.", PLUGIN_NAME);
    abort();
  } else {
    // Add all hooks.
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
  }
}
