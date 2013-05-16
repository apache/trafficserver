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

#include "ts/ts.h"
#include "ts/remap.h"
#include "ink_defs.h"

#include "lapi.h"
#include "lutil.h"
#include "state.h"
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t PluginInstanceLock = PTHREAD_MUTEX_INITIALIZER;

static TSRemapStatus
LuaPluginRemap(lua_State * lua, TSHttpTxn txn, TSRemapRequestInfo * rri)
{
  LuaRemapRequest * rq;

  lua_getglobal(lua, "remap");
  if (lua_isnil(lua, -1)) {
    // No "remap" callback, better continue.
    return TSREMAP_NO_REMAP;
  }

  LuaLogDebug("handling request %p on thread 0x%llx", rri, (unsigned long long)pthread_self());

  // XXX We can also cache the RemapRequestInfo in the Lua state. We we just need to reset
  // the rri pointer and status.
  rq = LuaPushRemapRequestInfo(lua, txn, rri);

  if (lua_pcall(lua, 1, 0, 0) != 0) {
    LuaLogDebug("remap failed: %s", lua_tostring(lua, -1));
    lua_pop(lua, 1);
    return TSREMAP_ERROR;
  }

  // XXX can we guarantee that rq has not been garbage collected?
  return rq->status;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info ATS_UNUSED */, char * /* errbuf ATS_UNUSED */,
            int /* errbuf_size ATS_UNUSED */)
{
  LuaLogDebug("loading lua plugin");

  // Allocate a TSHttpTxn argument index for handling per-transaction hooks.
  TSReleaseAssert(TSHttpArgIndexReserve("lua", "lua", &LuaHttpArgIndex) == TS_SUCCESS);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char * argv[], void ** ih, char * /* errbuf ATS_UNUSED */, int /* errsz ATS_UNUSED */)
{
  instanceid_t instanceid;

  pthread_mutex_lock(&PluginInstanceLock);

  // Register a new Lua plugin instance, skipping the first two arguments (which are the remap URLs).
  instanceid = LuaPluginRegister((unsigned)argc - 2, (const char **)argv + 2);
  *ih = (void *)(intptr_t)instanceid;

  pthread_mutex_unlock(&PluginInstanceLock);

  LuaLogDebug("created Lua remap instance %u", instanceid);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void * ih)
{
  instanceid_t instanceid = (intptr_t)ih;

  pthread_mutex_lock(&PluginInstanceLock);
  LuaPluginUnregister(instanceid);
  pthread_mutex_unlock(&PluginInstanceLock);
}

TSRemapStatus
TSRemapDoRemap(void * ih, TSHttpTxn txn, TSRemapRequestInfo * rri)
{
  ScopedLuaState lstate((intptr_t)ih);

  TSReleaseAssert(lstate);
  return LuaPluginRemap(lstate->lua, txn, rri);
}
