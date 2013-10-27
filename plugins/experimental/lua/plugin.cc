/*
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

#include <ts/ts.h>
#include "lutil.h"
#include "hook.h"
#include "state.h"

extern "C" void
TSPluginInit(int argc, const char * argv[])
{
  TSPluginRegistrationInfo  info;
  instanceid_t              instanceid;

  info.plugin_name = (char *)"lua";
  info.vendor_name = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
      LuaLogError("Plugin registration failed");
  }

  // Allocate a TSHttpTxn argument index for handling per-transaction hooks.
  TSReleaseAssert(TSHttpArgIndexReserve("lua", "lua", &LuaHttpArgIndex) == TS_SUCCESS);

  // Register a new Lua plugin instance, skipping the first argument (which is the plugin name).
  instanceid = LuaPluginRegister((unsigned)argc - 1, (const char **)argv + 1);
  TSReleaseAssert(instanceid == 0);
}

