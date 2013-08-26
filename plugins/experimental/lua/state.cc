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

#include "state.h"
#include "hook.h"
#include "lutil.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define INVALID_INSTANCE_ID (instanceid_t)(-1)

// InitDemuxTable() requires an initializer for every hook. Make sure that we don't
// get out of sync with the number of hooks.
extern void * __static_assert_hook_count[TS_HTTP_LAST_HOOK == 17 ? 0 : -1];

typedef int (*LuaHookDemuxer)(TSHttpHookID, TSCont, TSEvent, void *);

template <TSHttpHookID hookid, LuaHookDemuxer demuxer> int
DemuxSpecificHook(TSCont cont, TSEvent event, void * edata) {
  return demuxer(hookid, cont, event, edata);
}

template <LuaHookDemuxer demuxer> void
InitDemuxTable(LuaPluginInstance::demux_table_t& table)
{
#define MakeLuaHook(demuxer, hookid) TSContCreate(DemuxSpecificHook<hookid, demuxer>, NULL)

  table[TS_HTTP_READ_REQUEST_HDR_HOOK]  = MakeLuaHook(demuxer, TS_HTTP_READ_REQUEST_HDR_HOOK);
  table[TS_HTTP_OS_DNS_HOOK]            = MakeLuaHook(demuxer, TS_HTTP_OS_DNS_HOOK);
  table[TS_HTTP_SEND_REQUEST_HDR_HOOK]  = MakeLuaHook(demuxer, TS_HTTP_SEND_REQUEST_HDR_HOOK);
  table[TS_HTTP_READ_CACHE_HDR_HOOK]    = MakeLuaHook(demuxer, TS_HTTP_READ_CACHE_HDR_HOOK);
  table[TS_HTTP_READ_RESPONSE_HDR_HOOK] = MakeLuaHook(demuxer, TS_HTTP_READ_RESPONSE_HDR_HOOK);
  table[TS_HTTP_SEND_RESPONSE_HDR_HOOK] = MakeLuaHook(demuxer, TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  table[TS_HTTP_REQUEST_TRANSFORM_HOOK] = MakeLuaHook(demuxer, TS_HTTP_REQUEST_TRANSFORM_HOOK);
  table[TS_HTTP_RESPONSE_TRANSFORM_HOOK]= MakeLuaHook(demuxer, TS_HTTP_RESPONSE_TRANSFORM_HOOK);
  table[TS_HTTP_SELECT_ALT_HOOK]        = MakeLuaHook(demuxer, TS_HTTP_SELECT_ALT_HOOK);
  table[TS_HTTP_TXN_START_HOOK]         = MakeLuaHook(demuxer, TS_HTTP_TXN_START_HOOK);
  table[TS_HTTP_TXN_CLOSE_HOOK]         = MakeLuaHook(demuxer, TS_HTTP_TXN_CLOSE_HOOK);
  table[TS_HTTP_SSN_START_HOOK]         = MakeLuaHook(demuxer, TS_HTTP_SSN_START_HOOK);
  table[TS_HTTP_SSN_CLOSE_HOOK]         = MakeLuaHook(demuxer, TS_HTTP_SSN_CLOSE_HOOK);
  table[TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK] = MakeLuaHook(demuxer, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK);
  table[TS_HTTP_PRE_REMAP_HOOK]         = MakeLuaHook(demuxer, TS_HTTP_PRE_REMAP_HOOK);
  table[TS_HTTP_POST_REMAP_HOOK]        = MakeLuaHook(demuxer, TS_HTTP_POST_REMAP_HOOK);
  table[TS_HTTP_RESPONSE_CLIENT_HOOK]   = MakeLuaHook(demuxer, TS_HTTP_RESPONSE_CLIENT_HOOK);
}

// Global storage for Lua plugin instances. We vend instanceid_t's as an index into
// this array.
static std::vector<LuaPluginInstance *> LuaPluginStorage;

template <typename T> struct is_integral_type {
  enum { value = 0, is_pointer = 0 };
};

template <typename T> struct is_integral_type<T *> {
  enum { value = 1, is_pointer = 1 };
};

template <> struct is_integral_type<int> { enum { value = 1, is_pointer = 0 }; };
template <> struct is_integral_type<unsigned> { enum { value = 1, is_pointer = 0 }; };
template <> struct is_integral_type<long> { enum { value = 1, is_pointer = 0 }; };
template <> struct is_integral_type<unsigned long> { enum { value = 1, is_pointer = 0 }; };

static unsigned
nproc()
{
  long count;

  count = sysconf(_SC_NPROCESSORS_ONLN);
  return (unsigned)std::max(count, 1l);
}

static unsigned
thread_id()
{
  pthread_t self = pthread_self();

  if (is_integral_type<pthread_t>::value) {
    // If it's a pointer, then the lower bits are probably zero because it's
    // likely to be 8 or 16 byte aligned.
    if (is_integral_type<pthread_t>::is_pointer) {
      return (unsigned)((intptr_t)self >> 4);
    }
    return (unsigned)(intptr_t)self;
  } else {
    // XXX make this work on FreeBSD!
    TSReleaseAssert(0 && "unsupported platform");
    return 0;
  }
}

LuaPluginInstance::LuaPluginInstance()
  : instanceid(INVALID_INSTANCE_ID), paths(), states()
{
}

LuaPluginInstance::~LuaPluginInstance()
{
  this->invalidate();
}

void
LuaPluginInstance::invalidate()
{
  for (unsigned i = 0; i < this->states.size(); ++ i) {
    tsdelete(this->states[i]);
  }

  this->states.clear();
  this->paths.clear();
  this->instanceid = INVALID_INSTANCE_ID;

  for (unsigned i = 0; i < countof(this->demux.global); ++i) {
    TSContDestroy(this->demux.global[i]);
    TSContDestroy(this->demux.ssn[i]);
    TSContDestroy(this->demux.txn[i]);
    this->demux.global[i] = this->demux.ssn[i] = this->demux.txn[i] = NULL;
  }

}

void
LuaPluginInstance::init(unsigned argc, const char ** argv)
{
  for (unsigned i = 0; i < argc; ++i) {
    this->paths.push_back(argv[i]);
  }

  // Make sure we have enough threads to make concurrent access to lua
  // states unlikely.
  this->states.resize(nproc() * 2);

  InitDemuxTable<LuaDemuxGlobalHook>(this->demux.global);
  InitDemuxTable<LuaDemuxSsnHook>(this->demux.ssn);
  InitDemuxTable<LuaDemuxTxnHook>(this->demux.txn);

  for (unsigned i = 0; i < countof(this->demux.global); ++i) {
    TSContDataSet(this->demux.global[i], (void *)(uintptr_t)this->instanceid);
    TSContDataSet(this->demux.ssn[i], (void *)(uintptr_t)this->instanceid);
    TSContDataSet(this->demux.txn[i], (void *)(uintptr_t)this->instanceid);
  }

}

instanceid_t
LuaPluginRegister(unsigned argc, const char ** argv)
{
  instanceid_t instanceid = INVALID_INSTANCE_ID;
  LuaPluginInstance * plugin;

  LuaLogDebug("registering plugin");

  // OK, first we try to find an unused instance slot.
  for (unsigned i = 0; i < LuaPluginStorage.size(); ++i) {
    if (LuaPluginStorage[i] == NULL) {
      // This slot looks ok, let's try to claim it.
      instanceid = i;
      break;
    }
  }

  // Take the current instanceid, incrementing it for next time.
  if (instanceid == INVALID_INSTANCE_ID) {
    instanceid = LuaPluginStorage.size();
    LuaPluginStorage.resize(LuaPluginStorage.size() + 1);
  }

  // Mark this plugin instance as in use.
  plugin = LuaPluginStorage[instanceid] = tsnew<LuaPluginInstance>();
  plugin->instanceid = instanceid;

  // The path list should be empty if we correctly released it last time this
  // instance ID was used.
  TSReleaseAssert(plugin->paths.empty());
  LuaPluginStorage[instanceid]->init(argc, argv);

  // Allocate the Lua states, then separately initialize by evaluating all the Lua files.
  for (unsigned i = 0; i < plugin->states.size(); ++i) {
    plugin->states[i] = tsnew<LuaThreadState>();
    plugin->states[i]->alloc(plugin, i);
  }

  for (unsigned i = 0; i < LuaPluginStorage[instanceid]->states.size(); ++i) {
    plugin->states[i]->init(plugin);
  }

  return instanceid;
}

void
LuaPluginUnregister(instanceid_t instanceid)
{
  TSReleaseAssert(instanceid < LuaPluginStorage.size());
  tsdelete(LuaPluginStorage[instanceid]);
  LuaPluginStorage[instanceid] = NULL;
}

LuaThreadState::LuaThreadState()
  : lua(NULL), instance(NULL)
{
  pthread_mutexattr_t attr;

  // We need a recursive mutex so that we can safely reacquire it from Lua code.
  TSReleaseAssert(pthread_mutexattr_init(&attr) == 0);
  TSReleaseAssert(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0);
  TSReleaseAssert(pthread_mutex_init(&this->mutex, &attr) == 0);

  for (unsigned i = 0; i < countof(this->hookrefs); ++i) {
    this->hookrefs[i] = LUA_NOREF;
  }

  pthread_mutexattr_destroy(&attr);
}

LuaThreadState::~LuaThreadState()
{
  this->release();
  pthread_mutex_destroy(&this->mutex);
}

bool
LuaThreadState::alloc(LuaPluginInstance * plugin, unsigned threadid)
{
  this->lua = LuaNewState();
  this->instance = plugin;

  // Push the instanceid into a global integer. We will use this later to rendevous
  // with the lthread from the lua_State. We have to set the instanceid global before
  // executing any Lua code, because that will almost certainly call back into the plugin
  // ad reguire the instance id to be set.
  lua_pushinteger(this->lua, plugin->instanceid);
  lua_setfield(this->lua, LUA_REGISTRYINDEX, "__instanceid");

  lua_pushinteger(this->lua, threadid);
  lua_setfield(this->lua, LUA_REGISTRYINDEX, "__threadid");

  return true;
}

bool
LuaThreadState::init(LuaPluginInstance * plugin)
{
  for (LuaPluginInstance::pathlist_t::const_iterator p = plugin->paths.begin(); p < plugin->paths.end(); ++p) {
    LuaLogDebug("loading Lua program from %s", p->c_str());
    if (access(p->c_str(), F_OK) != 0) {
      LuaLogError("%s: %s", p->c_str(), strerror(errno));
      continue;
    }

    if (luaL_dofile(this->lua, p->c_str()) != 0) {
      // If the load failed, it should have pushed an error message.
      LuaLogError("failed to load Lua file %s: %s", p->c_str(), lua_tostring(lua, -1));
      return false;
    }
  }

  return true;
}

void
LuaThreadState::release()
{
  if (this->lua) {
    lua_close(this->lua);
    this->lua = NULL;
  }
}

std::pair<LuaThreadState *, LuaPluginInstance *>
LuaThreadStateAcquire(lua_State * lua)
{
  LuaThreadState * lthread;
  LuaPluginInstance * instance;
  instanceid_t instanceid;
  unsigned threadid;

  lua_getfield(lua, LUA_REGISTRYINDEX, "__instanceid");
  instanceid = (instanceid_t)luaL_checkinteger(lua, -1);

  lua_getfield(lua, LUA_REGISTRYINDEX, "__threadid");
  threadid = (unsigned)luaL_checkinteger(lua, -1);

  TSReleaseAssert(instanceid < LuaPluginStorage.size());

  instance = LuaPluginStorage[instanceid];

  TSReleaseAssert(threadid < instance->states.size());
  lthread = instance->states[threadid];

  LuaLogDebug("%u/%p acquired state %u from plugin instance %u on thread %u",
      instanceid, lthread->lua, threadid, instanceid, thread_id());

  lua_pop(lua, 2);

  // Since we already hav a lua_State, we must already be holding the lock. But acquire
  // and release come in matched pairs, so we need a recursive lock to release.
  TSReleaseAssert(pthread_mutex_lock(&lthread->mutex) == 0);
  return std::make_pair(lthread, instance);
}

std::pair<LuaThreadState *, LuaPluginInstance *>
LuaThreadStateAcquire(instanceid_t instanceid)
{
  LuaThreadState * lthread;
  LuaPluginInstance * instance;
  unsigned which;

  TSReleaseAssert(instanceid < LuaPluginStorage.size());

  instance = LuaPluginStorage[instanceid];

  // Index the set of LuaThreadStates with the thread ID. We might want to do a proper
  // hash on this to prevent false sharing.
  which = thread_id() % instance->states.size();
  lthread = instance->states[which];

  LuaLogDebug("%u/%p acquired state %u from plugin instance %u on thread %u",
      instanceid, lthread->lua, which, instanceid, thread_id());

  TSReleaseAssert(pthread_mutex_lock(&lthread->mutex) == 0);
  return std::make_pair(lthread, instance);
}

void
LuaThreadStateRelease(LuaThreadState * lthread)
{
  TSReleaseAssert(pthread_mutex_unlock(&lthread->mutex) == 0);
}
