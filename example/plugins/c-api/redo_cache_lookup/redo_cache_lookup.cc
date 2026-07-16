/** @file

  An example plugin to redo cache lookups with a fallback URL.

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

#include <string>
#include <string_view>

#include "ts/ts.h"
#include "redo_cache_lookup_config.h"

#define PLUGIN_NAME "redo_cache_lookup"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

struct RedoCacheLookupConfig {
  RedoCacheLookupConfig(std::string_view fallback) : fallback(fallback) {}

  std::string fallback;
};

int
handle_cache_lookup_complete(TSCont contp, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE) {
    return 0;
  }

  TSHttpTxn txnp   = static_cast<TSHttpTxn>(edata);
  auto     *config = static_cast<RedoCacheLookupConfig *>(TSContDataGet(contp));
  int       status = TS_CACHE_LOOKUP_MISS;

  if (TSHttpTxnCacheLookupStatusGet(txnp, &status) != TS_SUCCESS || status == TS_CACHE_LOOKUP_MISS ||
      status == TS_CACHE_LOOKUP_SKIPPED) {
    Dbg(dbg_ctl, "rewinding to check for fallback url: %s", config->fallback.c_str());
    TSHttpTxnRedoCacheLookup(txnp, config->fallback.c_str(), static_cast<int>(config->fallback.size()));
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  Dbg(dbg_ctl, "Init");
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  auto fallback = redo_cache_lookup::parse_fallback_url(argc, argv);

  if (!fallback) {
    Dbg(dbg_ctl, "Missing fallback option.");
    TSError("[%s] Missing fallback option", PLUGIN_NAME);
    return;
  }
  Dbg(dbg_ctl, "Initialized with fallback: %s", fallback->c_str());

  TSCont contp = TSContCreate(handle_cache_lookup_complete, nullptr);
  TSContDataSet(contp, new RedoCacheLookupConfig(*fallback));
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
}
