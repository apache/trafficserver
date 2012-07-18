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
#include <ts/remap.h>
#include <unistd.h>
#include <string.h>
#include "lapi.h"
#include "lutil.h"

static TSReturnCode
LuaPluginRelease(lua_State * lua)
{
  lua_getglobal(lua, "release");
  if (lua_isnil(lua, -1)) {
    // No "release" callback.
    return TS_SUCCESS;
  }

  if (lua_pcall(lua, 0, 0, 0) != 0) {
    LuaLogDebug("release failed: %s", lua_tostring(lua, -1));
    lua_pop(lua, 1);
  }

  lua_close(lua);
  return TS_SUCCESS;
}

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

void
TSRemapDeleteInstance(void * ih)
{
  lua_State * lua = (lua_State *)ih;

  if (lua) {
    LuaPluginRelease(lua);
    lua_close(lua);
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface * api_info, char * errbuf, int errbuf_size)
{
  LuaLogDebug("loading lua plugin");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char * argv[], void ** ih, char * errbuf, int errsz)
{
  LuaPluginState * remap;
  lua_State * lua;

  // Copy the plugin arguments so that we can use them to allocate a per-thread Lua state. It would be cleaner
  // to clone a Lua state, but there's no built-in way to do that, and to implement that ourselves would require
  // locking the template state (we need to manipulate the stack to copy values out).
  remap = tsnew<LuaPluginState>();
  remap->init((unsigned)argc, (const char **)argv);

  // Test whether we can successfully load the Lua program.
  lua = LuaPluginNewState(remap);
  if (!lua) {
    tsdelete(remap);
    return TS_ERROR;
  }

  *ih = remap;
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void * ih, TSHttpTxn txn, TSRemapRequestInfo * rri)
{
  LuaThreadInstance * lthread;

  // Find or clone the per-thread Lua state.
  lthread = LuaGetThreadInstance();
  if (!lthread) {
    LuaPluginState * lps;

    lps = (LuaPluginState *)ih;
    lthread = tsnew<LuaThreadInstance>();

    LuaLogDebug("allocating new Lua state on thread 0x%llx", (unsigned long long)pthread_self());
    lthread->lua = LuaPluginNewState(lps);
    LuaSetThreadInstance(lthread);
  }

  return LuaPluginRemap(lthread->lua, txn, rri);
}
