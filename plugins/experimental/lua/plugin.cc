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

extern "C" void
TSPluginInit(int argc, const char * argv[])
{
  LuaThreadInstance * lthread;
  TSPluginRegistrationInfo info;

  info.plugin_name = (char *)"lua";
  info.vendor_name = (char *)"Apache Traffic Server";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
      LuaLogError("Plugin registration failed");
  }

  TSAssert(LuaPlugin == NULL);

  // Allocate a TSHttpTxn argument index for handling per-transaction hooks.
  TSReleaseAssert(TSHttpArgIndexReserve(info.plugin_name, info.plugin_name, &LuaHttpArgIndex) == TS_SUCCESS);

  // Create the initial global Lua state.
  LuaPlugin = tsnew<LuaPluginState>();
  LuaPlugin->init((unsigned)argc, (const char **)argv);

  // Careful! We need to initialize the per-thread Lua state before we inject
  // any user code. User code will probably call TSAPI functions, which will
  // fetch or create the per-thread instance, which had better be available.
  lthread = tsnew<LuaThreadInstance>();
  lthread->lua = LuaPluginNewState();
  LuaSetThreadInstance(lthread);
  LuaPluginLoad(lthread->lua, LuaPlugin);
}

