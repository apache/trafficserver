/**
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

#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/Logger.h"
#include "tscpp/api/Stat.h"
#include "tscpp/api/PluginInit.h"
#include <cstring>

using namespace atscppapi;
using std::string;

namespace
{
// This is for the -T tag debugging
// To view the debug messages ./traffic_server -T "stat_example.*"
#define TAG "stat_example"

// This will be the actual stat name
// You can view it using traffic_ctl metric get stat_example
const string STAT_NAME = "stat_example";

// This is the stat we'll be using, you can view it's value
// using traffic_ctl metric get stat_example
Stat stat;

GlobalPlugin *plugin;
} // namespace

/*
 * This is a simple plugin that will increment a counter
 * every time a request comes in.
 */
class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    TS_DEBUG(TAG, "Registering a global hook HOOK_READ_REQUEST_HEADERS_POST_REMAP");
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    TS_DEBUG(TAG, "Received a request, incrementing the counter.");
    stat.increment();
    TS_DEBUG(TAG, "Stat '%s' value = %lld", STAT_NAME.c_str(), static_cast<long long>(stat.get()));
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_Stat", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  TS_DEBUG(TAG, "Loaded stat_example plugin");

  // Since this stat is not persistent it will be initialized to 0.
  stat.init(STAT_NAME, Stat::SYNC_COUNT, true);
  stat.set(0);

  plugin = new GlobalHookPlugin();
}
