/**
  @file
  @brief A plugin that prints the cache lookup status.

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
#include <ts/ts.h>   // for debug
#include <cstdlib>   // for abort
#include <cinttypes> // for PRId64
#include <string_view>
#include <unordered_map>

namespace
{
constexpr char const *PLUGIN_NAME = "print_cache_status";
static TSTextLogObject pluginlog;

std::unordered_map<int, std::string_view> lookup_status_to_string = {
  {TS_CACHE_LOOKUP_MISS, "TS_CACHE_LOOKUP_MISS"},
  {TS_CACHE_LOOKUP_HIT_STALE, "TS_CACHE_LOOKUP_HIT_STALE"},
  {TS_CACHE_LOOKUP_HIT_FRESH, "TS_CACHE_LOOKUP_HIT_FRESH"},
  {TS_CACHE_LOOKUP_SKIPPED, "TS_CACHE_LOOKUP_SKIPPED"},
};

int
global_handler(TSCont continuation, TSEvent event, void *data)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(data);

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    int obj_status = 0;
    if (TS_ERROR == TSHttpTxnCacheLookupStatusGet(txnp, &obj_status)) {
      TSError("[%s] TSHttpTxnCacheLookupStatusGet failed", PLUGIN_NAME);
    }
    TSTextLogObjectWrite(pluginlog, "Cache lookup status: %s", lookup_status_to_string[obj_status].data());
  } break;

  default:
    TSError("[%s] Unexpected event: %d", PLUGIN_NAME, event);
    return 0;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}
} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache";
  info.support_email = "bneradt@apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
  }
  TSAssert(TS_SUCCESS == TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &pluginlog));

  TSCont contp = TSContCreate(global_handler, TSMutexCreate());
  if (contp == nullptr) {
    TSError("[%s] could not create continuation.", PLUGIN_NAME);
    std::abort();
  } else {
    TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
  }
}
