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
#include <ink_config.h>

typedef TSCont HookDemuxTable[TS_HTTP_LAST_HOOK];

// Continuation tables for global, txn and ssn hooks. These are all indexed by the TSHttpHookID and
// are used to select which callback to invoke during event demuxing.
static struct
{
  HookDemuxTable global;
  HookDemuxTable txn;
  HookDemuxTable ssn;
} HttpHookDemuxTable;

const char *
HttpHookName(TSHttpHookID hookid)
{
  static const char * names[TS_HTTP_LAST_HOOK] = {
    "HTTP_READ_REQUEST_HDR_HOOK",
    "HTTP_OS_DNS_HOOK",
    "HTTP_SEND_REQUEST_HDR_HOOK",
    "HTTP_READ_CACHE_HDR_HOOK",
    "HTTP_READ_RESPONSE_HDR_HOOK",
    "HTTP_SEND_RESPONSE_HDR_HOOK",
    NULL, // XXX TS_HTTP_REQUEST_TRANSFORM_HOOK
    NULL, // XXX TS_HTTP_RESPONSE_TRANSFORM_HOOK
    NULL, // XXX HTTP_SELECT_ALT_HOOK
    "HTTP_TXN_START_HOOK",
    "HTTP_TXN_CLOSE_HOOK",
    "HTTP_SSN_START_HOOK",
    "HTTP_SSN_CLOSE_HOOK",
    "HTTP_CACHE_LOOKUP_COMPLETE_HOOK",
    "HTTP_PRE_REMAP_HOOK",
    "HTTP_POST_REMAP_HOOK",
  };

  if (hookid >= 0 && hookid < static_cast<TSHttpHookID>(countof(names))) {
    return names[hookid];
  }

  return NULL;
}

static bool
HookIsValid(int hookid)
{
  if (hookid == TS_HTTP_REQUEST_TRANSFORM_HOOK || hookid == TS_HTTP_RESPONSE_TRANSFORM_HOOK) {
    return false;
  }

  return hookid >= 0 && hookid < TS_HTTP_LAST_HOOK;
}

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
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
  case TS_EVENT_HTTP_PRE_REMAP:
  case TS_EVENT_HTTP_POST_REMAP:
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

// The per-ssn and per-txn argument mechanism stores a pointer, so it's NULL when not set. Unfortunately, 0 is a
// legitimate Lua reference value (all valies except LUA_NOREF are legitimate), so we can't distinguish NULL from a 0
// reference. In 64-bit mode we have some extra bits and we can maintain the state, but in 32-bit mode, we need to
// allocate the LuaHookReference to have enough space to store the state.

union LuaHookReference
{
  struct ref {
    bool  set;
    int   value;
  } ref;
  void * storage;
};

// For 64-bit pointers, we can inline the LuaHookReference, otherwise we need an extra malloc.
#if SIZEOF_VOID_POINTER >= 8
#define INLINE_LUA_HOOK_REFERENCE 1
#else
#undef INLINE_LUA_HOOK_REFERENCE
#endif

// Verify that LuaHookReference fits in sizeof(void *).
#if INLINE_LUA_HOOK_REFERENCE
extern char __LuaHookReferenceSizeCheck[sizeof(LuaHookReference) == SIZEOF_VOID_POINTER ? 0 : -1];
#endif

static void *
LuaHttpObjectArgGet(TSHttpSsn ssn)
{
  return TSHttpSsnArgGet(ssn, LuaHttpArgIndex);
}

static void *
LuaHttpObjectArgGet(TSHttpTxn txn)
{
  return TSHttpTxnArgGet(txn, LuaHttpArgIndex);
}

static void
LuaHttpObjectArgSet(TSHttpSsn ssn, void * ptr)
{
  return TSHttpSsnArgSet(ssn, LuaHttpArgIndex, ptr);
}

static void
LuaHttpObjectArgSet(TSHttpTxn txn, void * ptr)
{
  return TSHttpTxnArgSet(txn, LuaHttpArgIndex, ptr);
}

template<typename T> static int
LuaGetArgReference(T ptr)
{
  LuaHookReference href;

  href.storage = LuaHttpObjectArgGet(ptr);

#if !defined(INLINE_LUA_HOOK_REFERENCE)
  if (href.storage) {
    href  = *(LuaHookReference *)href.storage;
  }
#endif

  return (href.ref.set) ? href.ref.value : LUA_NOREF;
}

template <typename T> void
LuaSetArgReference(T ptr, int ref)
{
  LuaHookReference href;

  href.storage = NULL;
  href.ref.value = ref;
  href.ref.set = true;

#if defined(INLINE_LUA_HOOK_REFERENCE)
  LuaHttpObjectArgSet(ptr, href.storage);
#else
  LuaHookReference * tmp = (LuaHookReference *)LuaHttpObjectArgGet(ptr);
  if (tmp) {
    *tmp = href;
  } else {
    tmp = (LuaHookReference *)TSmalloc(sizeof(LuaHookReference));
    *tmp = href;
    LuaHttpObjectArgSet(ptr, tmp);
  }
#endif
}

template <typename T> static void
LuaClearArgReference(T ptr)
{
#if !defined(INLINE_LUA_HOOK_REFERENCE)
  TSfree(LuaHttpObjectArgGet(ptr));
#endif
  LuaHttpObjectArgSet(ptr, NULL);
}

// Force template instantiation of LuaSetArgReference().
template void LuaSetArgReference<TSHttpSsn>(TSHttpSsn ssn, int ref);
template void LuaSetArgReference<TSHttpTxn>(TSHttpTxn txn, int ref);

static LuaThreadInstance *
LuaDemuxThreadInstance()
{
  LuaThreadInstance * lthread;

  lthread = LuaGetThreadInstance();

  if (lthread == NULL) {
    lthread = tsnew<LuaThreadInstance>();
    lthread->lua = LuaPluginNewState();
    LuaSetThreadInstance(lthread);
    LuaPluginLoad(lthread->lua, LuaPlugin);
  }

  return lthread;
}

static TSHttpHookID
LuaDemuxHookID(TSCont cont)
{
  TSHttpHookID hookid = (TSHttpHookID)(intptr_t)TSContDataGet(cont);
  TSAssert(HookIsValid(hookid));
  return hookid;
}

static void
LuaDemuxInvokeCallback(lua_State * lua, TSHttpHookID hookid, TSEvent event, void * edata, int ref)
{
  int nitems = lua_gettop(lua);

  // Push the callback table onto the top of the stack.
  lua_rawgeti(lua, LUA_REGISTRYINDEX, ref);

  // XXX If this is a global hook, we have a function reference. If it's a ssn or txn hook then we
  // have a callback table reference. We need to make these the same, but not rught now ...

  switch (lua_type(lua, -1)) {
    case LUA_TFUNCTION:
      // Nothing to do, the function we want to invoke is already on top of the stack.
      break;
    case LUA_TTABLE:
      // Push the hookid onto the stack so we can use it to index the table (that is now at -2).
      lua_pushinteger(lua, hookid);

      TSAssert(lua_isnumber(lua, -1));
      TSAssert(lua_istable(lua, -2));

      // Index the callback table with the hookid to get the callback function for this hook.
      lua_gettable(lua, -2);

      break;
    default:
      LuaLogError("invalid callback reference type %s", ltypeof(lua, -1));
      TSReleaseAssert(0);
  }

  // The item on the top of the stack *ought* to be the callback function. However when we register a cleanup function
  // to release the callback reference (because the ssn ot txn closes), then we won't have a function because there's
  // nothing to do here.
  if (!lua_isnil(lua, -1)) {

    TSAssert(lua_isfunction(lua, -1));

    lua_pushinteger(lua, event);
    LuaPushEventData(lua, event, edata);

    if (lua_pcall(lua, 2 /* nargs */, 0, 0) != 0) {
      LuaLogDebug("hook callback failed: %s", lua_tostring(lua, -1));
      lua_pop(lua, 1); // pop the error message
    }
  }

  // If we left anything on the stack, pop it.
  lua_pop(lua, lua_gettop(lua) - nitems);
}

static int
LuaDemuxGlobalHook(TSCont cont, TSEvent event, void * edata)
{
  TSHttpHookID        hookid = LuaDemuxHookID(cont);
  LuaThreadInstance * lthread = LuaDemuxThreadInstance();
  int                 ref = lthread->hooks[hookid];

  LuaLogDebug("%s lthread=%p event=%d edata=%p, ref=%d",
      HttpHookName(hookid), lthread, event, edata, ref);

  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lthread->lua, hookid, event, edata, ref);
  return TS_EVENT_NONE;
}

static int
LuaDemuxTxnHook(TSCont cont, TSEvent event, void * edata)
{
  TSHttpHookID        hookid = LuaDemuxHookID(cont);
  LuaThreadInstance * lthread = LuaDemuxThreadInstance();
  int                 ref = LuaGetArgReference((TSHttpTxn)edata);

  LuaLogDebug("%s(%s) lthread=%p event=%d edata=%p",
      __func__, HttpHookName(hookid), lthread, event, edata);

  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lthread->lua, hookid, event, edata, ref);

  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    LuaLogDebug("unref event handler %d", ref);
    luaL_unref(lthread->lua, LUA_REGISTRYINDEX, ref);
    LuaClearArgReference((TSHttpTxn)edata);
  }

  return TS_EVENT_NONE;
}

static int
LuaDemuxSsnHook(TSCont cont, TSEvent event, void * edata)
{
  TSHttpHookID        hookid = LuaDemuxHookID(cont);
  LuaThreadInstance * lthread = LuaDemuxThreadInstance();
  TSHttpSsn           ssn;
  int                 ref;

  // The edata might be a Txn or a Ssn, depending on the event type. If we get here, it's because we registered a
  // callback on the Ssn, so we need to get back to the Ssn object in order to the the callback table reference ...
  switch (event) {
    case TS_EVENT_HTTP_SSN_START:
    case TS_EVENT_HTTP_SSN_CLOSE:
      ssn = (TSHttpSsn)edata;
      break;
    default:
      ssn = TSHttpTxnSsnGet((TSHttpTxn)edata);
  }

  LuaLogDebug("%s(%s) lthread=%p event=%d edata=%p",
      __func__, HttpHookName(hookid), lthread, event, edata);

  ref = LuaGetArgReference(ssn);
  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lthread->lua, hookid, event, edata, ref);

  if (event == TS_EVENT_HTTP_SSN_CLOSE) {
    LuaLogDebug("unref event handler %d", ref);
    luaL_unref(lthread->lua, LUA_REGISTRYINDEX, ref);
    LuaClearArgReference((TSHttpSsn)edata);
  }

  return TS_EVENT_NONE;
}

bool
LuaRegisterHttpHooks(lua_State * lua, void * obj, LuaHookAddFunction add, int hooks)
{
  bool hooked_close = false;
  const TSHttpHookID closehook = (add == LuaHttpSsnHookAdd ? TS_HTTP_SSN_CLOSE_HOOK : TS_HTTP_TXN_CLOSE_HOOK);

  TSAssert(add == LuaHttpSsnHookAdd || add == LuaHttpTxnHookAdd);

  // Push the hooks reference back onto the stack.
  lua_rawgeti(lua, LUA_REGISTRYINDEX, hooks);

  // The value on the top of the stack (index -1) MUST be the callback table.
  TSAssert(lua_istable(lua, lua_gettop(lua)));

  // Walk the table and register the hook for each entry.
  lua_pushnil(lua);  // Push the first key, makes the callback table index -2.
  while (lua_next(lua, -2) != 0) {
    TSHttpHookID hookid;

    // uses 'key' (at index -2) and 'value' (at index -1).
    // LuaLogDebug("key=%s value=%s\n", ltypeof(lua, -2), ltypeof(lua, -1));

    // Now the key (index -2) and value (index -1) got pushed onto the stack. The key must be a hook ID and the value
    // must be a callback function.
    luaL_checktype(lua, -1, LUA_TFUNCTION);
    hookid = (TSHttpHookID)luaL_checkint(lua, -2);

    if (!HookIsValid(hookid)) {
      LuaLogError("invalid Hook ID %d", hookid);
      goto next;
    }

    if (hookid == closehook) {
      hooked_close = true;
    }

    // At demux time, we need the hook ID and the table (or function) ref.
    add(obj, hookid);
    LuaLogDebug("registered callback table %d for event %s on object %p",
        hooks, HttpHookName(hookid), obj);

next:
    // Pop the value (index -1), leaving key as the new top (index -1).
    lua_pop(lua, 1);
  }

  // we always need to hook the close because we keep a reference to the callback table and we need to release that
  // reference when the object's lifetime ends.
  if (!hooked_close) {
    add(obj, closehook);
  }

  return true;
}

void
LuaHttpSsnHookAdd(void * ssn, TSHttpHookID hookid)
{
  TSHttpSsnHookAdd((TSHttpSsn)ssn, hookid, HttpHookDemuxTable.ssn[hookid]);
}

void
LuaHttpTxnHookAdd(void * txn, TSHttpHookID hookid)
{
  TSHttpTxnHookAdd((TSHttpTxn)txn, hookid, HttpHookDemuxTable.txn[hookid]);
}

static int
TSLuaHttpHookRegister(lua_State * lua)
{
  TSHttpHookID hookid;
  LuaThreadInstance * lthread;

  LuaLogDebug("[1]=%s [2]=%s", ltypeof(lua, 1), ltypeof(lua, 2));
  hookid = (TSHttpHookID)luaL_checkint(lua, 1);
  luaL_checktype(lua, 2, LUA_TFUNCTION);

  LuaLogDebug("registering hook %s (%d)", HttpHookName(hookid), (int)hookid);
  if (hookid < 0 || hookid >= TS_HTTP_LAST_HOOK) {
    LuaLogDebug("hook ID %d out of range", hookid);
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
    TSReleaseAssert(HttpHookDemuxTable.global[hookid] != NULL);
    return 0;
  }

  lthread->hooks[hookid] = luaL_ref(lua, LUA_REGISTRYINDEX);

  if (HttpHookDemuxTable.global[hookid] == NULL) {
    TSCont cont;

    cont = TSContCreate(LuaDemuxGlobalHook, TSMutexCreate());
    if (__sync_bool_compare_and_swap(&HttpHookDemuxTable.global[hookid], NULL, cont)) {
      LuaLogDebug("installed continuation for %s", HttpHookName(hookid));
      TSContDataSet(cont, (void *)hookid);
      TSHttpHookAdd(hookid, cont);
    } else {
      LuaLogDebug("lost hook creation race for %s", HttpHookName(hookid));
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
  LuaLogDebug("initializing TS Hook API");

  lua_newtable(lua);

  // Register functions in the "ts.hook" module.
  luaL_register(lua, NULL, LUAEXPORTS);

  for (unsigned i = 0; i < TS_HTTP_LAST_HOOK; ++i) {
    if (HttpHookName((TSHttpHookID)i) == NULL) {
      // Unsupported hook, skip it.
      continue;
    }

    // Register named constants for each hook ID.
    LuaSetConstantField(lua, HttpHookName((TSHttpHookID)i), i);
    // Allocate txn and ssn continuations.
    HttpHookDemuxTable.txn[i] = TSContCreate(LuaDemuxTxnHook, NULL);
    HttpHookDemuxTable.ssn[i] = TSContCreate(LuaDemuxSsnHook, NULL);
    // And keep track of which hook each continuation was allocated for.
    TSContDataSet(HttpHookDemuxTable.txn[i], (void *)(uintptr_t)i);
    TSContDataSet(HttpHookDemuxTable.ssn[i], (void *)(uintptr_t)i);

    // Note that we allocate the global continuation table lazily so that we know when to add the hook.
  }

  return 1;
}
