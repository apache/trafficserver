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
#include "state.h"

#include <memory> // placement new

#include "ink_config.h"
#include "ink_defs.h"

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


#if defined(INLINE_LUA_HOOK_REFERENCE)
typedef char __size_check[sizeof(this_type) == sizeof(void *) ? 0 : -1];
#endif

// For 64-bit pointers, we can inline the LuaHookReference, otherwise we need an extra malloc.
//
#if SIZEOF_VOID_POINTER >= 8
#define INLINE_LUA_HOOK_REFERENCE 1
#else
#undef INLINE_LUA_HOOK_REFERENCE
#endif

template <typename t1, typename t2>
struct inline_tuple
{
  typedef t1 first_type;
  typedef t2 second_type;
  typedef inline_tuple<first_type, second_type> this_type;

  union {
    struct {
      first_type first;
      second_type second;
    } s;
    void * ptr;
  } storage;

  first_type& first() { return storage.s.first; }
  second_type& second() { return storage.s.second; }

  static void * allocate(const first_type first, const second_type second) {
#if defined(INLINE_LUA_HOOK_REFERENCE)
    this_type obj;
    obj.first() = first;
    obj.second() = second;
    return obj.storage.ptr;
#else
    this_type * ptr = (this_type *)TSmalloc(sizeof(this_type));
    ptr->first() = first;
    ptr->second() = second;
    return ptr;
#endif
  }

  static void free(void *ptr ATS_UNUSED) {
#if defined(INLINE_LUA_HOOK_REFERENCE)
    // Nothing to do, because we never allocated.
#else
    TSfree(ptr);
#endif
  }

  static this_type get(void * ptr) {
#if defined(INLINE_LUA_HOOK_REFERENCE)
    this_type obj;
    obj.storage.ptr = ptr;
    return obj;
#else
    return ptr ? *(this_type *)ptr : this_type();
#endif
  }

};

// The per-ssn and per-txn argument mechanism stores a pointer, so it's NULL when not set. Unfortunately, 0 is a
// legitimate Lua reference value (all values except LUA_NOREF are legitimate), so we can't distinguish NULL from a 0
// reference. In 64-bit mode we have some extra bits and we can maintain the state, but in 32-bit mode, we need to
// allocate the LuaHookReference to have enough space to store the state.
typedef inline_tuple<int, bool> LuaHookReference;

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
  LuaHookReference href(LuaHookReference::get(LuaHttpObjectArgGet(ptr)));
  // Only return the Lua ref if it was previously set.
  return href.second() ? href.first() : LUA_NOREF;
}

template <typename T> void
LuaSetArgReference(T ptr, int ref)
{
  LuaHookReference::free(LuaHttpObjectArgGet(ptr));
  LuaHttpObjectArgSet(ptr, LuaHookReference::allocate(ref, true));
}

template <typename T> static void
LuaClearArgReference(T ptr)
{
  LuaHookReference::free(LuaHttpObjectArgGet(ptr));
  LuaHttpObjectArgSet(ptr, NULL);
}

// Force template instantiation of LuaSetArgReference().
template void LuaSetArgReference<TSHttpSsn>(TSHttpSsn ssn, int ref);
template void LuaSetArgReference<TSHttpTxn>(TSHttpTxn txn, int ref);

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

  // The item on the top of the stack *ought* to be the callback function. However when we register a
  // cleanup function to release the callback reference (because the ssn or txn closes), then we won't
  // have a function because there's nothing to do here.
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

int
LuaDemuxGlobalHook(TSHttpHookID hookid, TSCont cont, TSEvent event, void * edata)
{
  instanceid_t        instanceid = (uintptr_t)TSContDataGet(cont);
  ScopedLuaState      lstate(instanceid);
  int                 ref = lstate->hookrefs[hookid];

  LuaLogDebug("%u/%p %s event=%d edata=%p, ref=%d",
      instanceid, lstate->lua,
      HttpHookName(hookid), event, edata, ref);

  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lstate->lua, hookid, event, edata, ref);
  return TS_EVENT_NONE;
}

int
LuaDemuxTxnHook(TSHttpHookID hookid, TSCont cont, TSEvent event, void * edata)
{
  int                 ref = LuaGetArgReference((TSHttpTxn)edata);
  instanceid_t        instanceid = (uintptr_t)TSContDataGet(cont);
  ScopedLuaState      lstate(instanceid);

  LuaLogDebug("%s(%s) instanceid=%u event=%d edata=%p",
      __func__, HttpHookName(hookid), instanceid, event, edata);

  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lstate->lua, hookid, event, edata, ref);

  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    LuaLogDebug("unref event handler %d", ref);
    luaL_unref(lstate->lua, LUA_REGISTRYINDEX, ref);
    LuaClearArgReference((TSHttpTxn)edata);
  }

  return TS_EVENT_NONE;
}

int
LuaDemuxSsnHook(TSHttpHookID hookid, TSCont cont, TSEvent event, void * edata)
{
  instanceid_t        instanceid = (uintptr_t)TSContDataGet(cont);
  ScopedLuaState      lstate(instanceid);
  TSHttpSsn           ssn;
  int                 ref;

  // The edata might be a Txn or a Ssn, depending on the event type. If we get here, it's because we
  // registered a callback on the Ssn, so we need to get back to the Ssn object in order to the the
  // callback table reference ...
  switch (event) {
    case TS_EVENT_HTTP_SSN_START:
    case TS_EVENT_HTTP_SSN_CLOSE:
      ssn = (TSHttpSsn)edata;
      break;
    default:
      ssn = TSHttpTxnSsnGet((TSHttpTxn)edata);
  }

  LuaLogDebug("%s(%s) instanceid=%u event=%d edata=%p",
      __func__, HttpHookName(hookid), instanceid, event, edata);

  ref = LuaGetArgReference(ssn);
  if (ref == LUA_NOREF) {
    LuaLogError("no Lua callback for hook %s", HttpHookName(hookid));
    return TS_EVENT_ERROR;
  }

  LuaDemuxInvokeCallback(lstate->lua, hookid, event, edata, ref);

  if (event == TS_EVENT_HTTP_SSN_CLOSE) {
    LuaLogDebug("unref event handler %d", ref);
    luaL_unref(lstate->lua, LUA_REGISTRYINDEX, ref);
    LuaClearArgReference((TSHttpSsn)edata);
  }

  return TS_EVENT_NONE;
}

bool
LuaRegisterHttpHooks(lua_State * lua, void * obj, LuaHookAddFunction add, int hooks)
{
  bool                hooked_close = false;
  const TSHttpHookID  closehook = (add == LuaHttpSsnHookAdd ? TS_HTTP_SSN_CLOSE_HOOK : TS_HTTP_TXN_CLOSE_HOOK);

  TSAssert(add == LuaHttpSsnHookAdd || add == LuaHttpTxnHookAdd);

  // Push the hooks reference back onto the stack.
  lua_rawgeti(lua, LUA_REGISTRYINDEX, hooks);

  // The value on the top of the stack (index -1) MUST be the callback table.
  TSAssert(lua_istable(lua, lua_gettop(lua)));

  // Now we need our LuaThreadState to access the hook tables.
  ScopedLuaState lstate(lua);

  // Walk the table and register the hook for each entry.
  lua_pushnil(lua);  // Push the first key, makes the callback table index -2.
  while (lua_next(lua, -2) != 0) {
    TSHttpHookID hookid;

    // uses 'key' (at index -2) and 'value' (at index -1).
    // LuaLogDebug("key=%s value=%s\n", ltypeof(lua, -2), ltypeof(lua, -1));

    // Now the key (index -2) and value (index -1) got pushed onto the stack. The key must be a hook ID and
    // the value must be a callback function.
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
    add(obj, lstate.instance(), hookid);
    LuaLogDebug("registered callback table %d for event %s on object %p",
        hooks, HttpHookName(hookid), obj);

next:
    // Pop the value (index -1), leaving key as the new top (index -1).
    lua_pop(lua, 1);
  }

  // we always need to hook the close because we keep a reference to the callback table and we need to
  // release that reference when the object's lifetime ends.
  if (!hooked_close) {
    add(obj, lstate.instance(), closehook);
  }

  return true;
}

void
LuaHttpSsnHookAdd(void * ssn, const LuaPluginInstance * instance, TSHttpHookID hookid)
{
  TSHttpSsnHookAdd((TSHttpSsn)ssn, hookid, instance->demux.ssn[hookid]);
}

void
LuaHttpTxnHookAdd(void * txn, const LuaPluginInstance * instance, TSHttpHookID hookid)
{
  TSHttpTxnHookAdd((TSHttpTxn)txn, hookid, instance->demux.txn[hookid]);
}

static int
TSLuaHttpHookRegister(lua_State * lua)
{
  TSHttpHookID      hookid;

  hookid = (TSHttpHookID)luaL_checkint(lua, 1);
  luaL_checktype(lua, 2, LUA_TFUNCTION);

  LuaLogDebug("registering hook %s (%d)", HttpHookName(hookid), (int)hookid);
  if (hookid < 0 || hookid >= TS_HTTP_LAST_HOOK) {
    LuaLogDebug("hook ID %d out of range", hookid);
    return -1;
  }

  ScopedLuaState lstate(lua);
  TSReleaseAssert(lstate);

  // The lstate must match the current Lua state or something is seriously wrong.
  TSReleaseAssert(lstate->lua == lua);

  // Global hooks can only be registered once, but we load the Lua scripts in every thread. Check whether
  // the hook has already been registered and ignore any double-registrations.
  if (lstate->hookrefs[hookid] != LUA_NOREF) {
    LuaLogDebug("ignoring double registration for %s hook", HttpHookName(hookid));
    return 0;
  }

  // The callback function for the hook should be on the top of the stack now. Keep a reference
  // to the callback function in the registry so we can pop it out later.
  TSAssert(lua_type(lua, lua_gettop(lua)) == LUA_TFUNCTION);
  lstate->hookrefs[hookid] = luaL_ref(lua, LUA_REGISTRYINDEX);

  LuaLogDebug("%u/%p added hook ref %d for %s",
      lstate->instance->instanceid, lua, lstate->hookrefs[hookid], HttpHookName(hookid));

  // We need to atomically install this global hook. We snaffle the high bit to mark whether or
  // not it has been installed.
  if (((uintptr_t)lstate->instance->demux.global[hookid] & 0x01u) == 0) {
    TSCont cont = (TSCont)((uintptr_t)lstate->instance->demux.global[hookid] | 0x01u);

    if (__sync_bool_compare_and_swap(&lstate->instance->demux.global[hookid],
          lstate->instance->demux.global[hookid], cont)) {
      LuaLogDebug("installed continuation for %s", HttpHookName(hookid));
      TSHttpHookAdd(hookid, (TSCont)((uintptr_t)cont & ~0x01u));
    } else {
      LuaLogDebug("lost hook creation race for %s", HttpHookName(hookid));
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
    if (HttpHookName((TSHttpHookID)i) != NULL) {
      // Register named constants for each hook ID.
      LuaSetConstantField(lua, HttpHookName((TSHttpHookID)i), i);
    }
  }

  return 1;
}
