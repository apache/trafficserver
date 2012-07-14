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
#include <string.h>
#include "lapi.h"
#include "lutil.h"
#include "hook.h"

#include <memory> // placement new

struct HookDemuxEntry
{
  const char *  name;
  int           hookid;
  TSCont        cont;
};

#define HOOK_DEMUX_ENTRY(HOOKID) { #HOOKID, TS_ ## HOOKID, NULL }

static HookDemuxEntry
HttpHookDemuxTable[TS_HTTP_LAST_HOOK] = {
  HOOK_DEMUX_ENTRY(HTTP_READ_REQUEST_HDR_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_OS_DNS_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_SEND_REQUEST_HDR_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_READ_CACHE_HDR_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_READ_RESPONSE_HDR_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_SEND_RESPONSE_HDR_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_REQUEST_TRANSFORM_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_RESPONSE_TRANSFORM_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_SELECT_ALT_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_TXN_START_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_TXN_CLOSE_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_SSN_START_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_SSN_CLOSE_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_CACHE_LOOKUP_COMPLETE_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_PRE_REMAP_HOOK),
  HOOK_DEMUX_ENTRY(HTTP_POST_REMAP_HOOK),
};

static void
LuaPushEventData(lua_State * lua, TSEvent event, void * edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
  case TS_EVENT_HTTP_OS_DNS:
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
  case TS_EVENT_HTTP_READ_CACHE_HDR:
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
  case TS_EVENT_HTTP_SELECT_ALT:
  case TS_EVENT_HTTP_TXN_START:
  case TS_EVENT_HTTP_TXN_CLOSE:
  case TS_EVENT_CACHE_LOOKUP_COMPLETE:
    LuaPushHttpTransaction(lua, (TSHttpTxn)edata);
    break;
  case TS_EVENT_HTTP_SSN_START:
  case TS_EVENT_HTTP_SSN_CLOSE:
    LuaPushHttpSession(lua, (TSHttpSsn)edata);
    break;
  default:
    lua_pushnil(lua);
  }
}

// XXX this is bollocks ... need to keep the hook ID in the continuation. The demuxer has to be per-thread
// because it holds references to the per-thread Lua state. If we keep the demuxer on the continuation,
// then it's per hook which is not gonna work.

static int
LuaHookDemux(TSCont cont, TSEvent event, void * edata)
{
  TSHttpHookID hookid = (TSHttpHookID)(intptr_t)TSContDataGet(cont);
  LuaThreadInstance * lthread;

  lthread = LuaGetThreadInstance();

  TSDebug("lua", "%s(%s) lthread=%p event=%d edata=%p",
      __func__, HttpHookDemuxTable[hookid].name, lthread, event, edata);

  if (lthread == NULL) {
    lthread = tsnew<LuaThreadInstance>();
    lthread->lua = LuaPluginNewState();
    LuaSetThreadInstance(lthread);
    LuaPluginLoad(lthread->lua, LuaPlugin);
  }

  TSAssert(hookid >= 0);
  TSAssert(hookid < TS_HTTP_LAST_HOOK);

  if (lthread->hooks[hookid] != LUA_NOREF) {
    lua_rawgeti(lthread->lua, LUA_REGISTRYINDEX, lthread->hooks[hookid]);
    lua_pushinteger(lthread->lua, event);
    LuaPushEventData(lthread->lua, event, edata);
    if (lua_pcall(lthread->lua, 2 /* nargs */, 0, 0) != 0) {
      TSDebug("lua", "hook callback failed: %s", lua_tostring(lthread->lua, -1));
      lua_pop(lthread->lua, 1); // pop the error message
    }
  } else {
    TSDebug("lua", "no demuxer for %s", HttpHookDemuxTable[hookid].name);
  }

  return 0;
}

static int
TSLuaHttpHookRegister(lua_State * lua)
{
  TSHttpHookID hookid;
  LuaThreadInstance * lthread;

  hookid = (TSHttpHookID)luaL_checkint(lua, 1);
  luaL_checktype(lua, 2, LUA_TFUNCTION);

  if (hookid < 0 || hookid >= TS_HTTP_LAST_HOOK) {
    TSDebug("lua", "hook ID %d out of range", hookid);
    return -1;
  }

  lthread = LuaGetThreadInstance();
  if (lthread == NULL) {
    lthread = tsnew<LuaThreadInstance>();
    lthread->lua = LuaPluginNewState(LuaPlugin);
    LuaSetThreadInstance(lthread);
  }

  // Global hooks can only be registered once, but we load the Lua scripts in every thread. Check whether the hook has
  // already been registered and ignore any double-registrations.
  if (lthread->hooks[hookid] != LUA_NOREF) {
    TSReleaseAssert(HttpHookDemuxTable[hookid].cont != NULL);
    return 0;
  }

  lthread->hooks[hookid] = luaL_ref(lua, LUA_REGISTRYINDEX);

  if (HttpHookDemuxTable[hookid].cont == NULL) {
    TSCont cont;

    cont = TSContCreate(LuaHookDemux, TSMutexCreate());
    if (__sync_bool_compare_and_swap(&HttpHookDemuxTable[hookid].cont, NULL, cont)) {
      TSDebug("lua", "installed hook for %s", HttpHookDemuxTable[hookid].name);
      TSContDataSet(cont, (void *)hookid);
      TSHttpHookAdd(hookid, cont);
    } else {
      TSDebug("lua", "lost hook creation race for %s", HttpHookDemuxTable[hookid].name);
      TSContDestroy(cont);
    }
  }

  return 0;
}

static const luaL_Reg LUAEXPORTS[] =
{
  { "register", TSLuaHttpHookRegister },
  { NULL, NULL}
};

int
LuaHookApiInit(lua_State * lua)
{
  TSDebug("lua", "initializing TS Hook API");

  lua_newtable(lua);

  // Register functions in the "ts.hook" module.
  luaL_register(lua, NULL, LUAEXPORTS);

  for (unsigned i = 0; i < arraysz(HttpHookDemuxTable); ++i) {
    if (HttpHookDemuxTable[i].name == NULL) {
      continue;
    }

    TSAssert((int)i == HttpHookDemuxTable[i].hookid);
    LuaSetConstantField(lua, HttpHookDemuxTable[i].name, HttpHookDemuxTable[i].hookid);
  }

  return 1;
}
