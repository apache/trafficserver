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

#ifndef LUA_STATE_H_
#define LUA_STATE_H_

#include <string>
#include <vector>
#include <utility>
#include <pthread.h>
#include <lua.hpp>

/*

Lua Plugin threading model

For remapping, we need to support multiple indepedent Lua plugin
instances. Each instance is handled by a LuaPluginInstance object.
Each plugin instance maintains a pool of lua_States which are
independent Lua interpeters. The LuaThreadState object owns a single
lua_State, holding additional hook data that is needed to de-multiplex
events.

There are two basic code paths to obtaining a LuaThreadState. If
we already have a lua_State, then we can use the __instanceid and
__threadid global variables to identify the LuaThreadState object.
If we don't have a lua_State, then we know the instance ID from the
hook continuation data (attached per LuaPluginInstance), and we
choose a state by hashing the thread ID.

  Traffic Server +-> LuaPluginInstance[0]
                 |   +-> LuaThreadState[0]
                 |   +-> LuaThreadState[1]
                 |   +-> LuaThreadState[2]
                 |   +-> LuaThreadState[3]
                 |
                 +-> LuaPluginInstance[1]
                 |   +-> LuaThreadState[0]
                 |   +-> LuaThreadState[1]
                 |   +-> LuaThreadState[2]
                 |   +-> LuaThreadState[3]
                 |
                 +-> LuaPluginInstance[2]
                     +-> LuaThreadState[0]
                     +-> LuaThreadState[1]
                     +-> LuaThreadState[2]
                     +-> LuaThreadState[3]

*/

typedef uint32_t instanceid_t;

struct LuaThreadState;
struct LuaPluginInstance;

// Per-thread lua_State. Used to execute Lua-side code in ethreads.
struct LuaThreadState
{
  lua_State *         lua;
  int                 hookrefs[TS_HTTP_LAST_HOOK];
  LuaPluginInstance * instance;

  pthread_mutex_t mutex;

  LuaThreadState();
  ~LuaThreadState();

  bool alloc(LuaPluginInstance *, unsigned);
  bool init(LuaPluginInstance *);
  void release();

private:
  LuaThreadState(const LuaThreadState&); // disable
  LuaThreadState& operator=(const LuaThreadState&); // disable
};

struct LuaPluginInstance
{
  typedef std::vector<std::string> pathlist_t;
  typedef TSCont demux_table_t[TS_HTTP_LAST_HOOK];

  LuaPluginInstance();
  ~LuaPluginInstance();

  void invalidate();
  void init(unsigned argc, const char ** argv);

  struct {
    demux_table_t global;
    demux_table_t txn;
    demux_table_t ssn;
  } demux;

  instanceid_t  instanceid;
  pathlist_t    paths;
  std::vector<LuaThreadState *> states;

private:
  LuaPluginInstance(const LuaPluginInstance&); // disable
  LuaPluginInstance& operator=(const LuaPluginInstance&); // disable
};

instanceid_t LuaPluginRegister(unsigned argc, const char ** argv);
void LuaPluginUnregister(instanceid_t instanceid);

// Acquire a locked Lua thread state belonging to the given instance.
std::pair<LuaThreadState *, LuaPluginInstance *> LuaThreadStateAcquire(instanceid_t);
std::pair<LuaThreadState *, LuaPluginInstance *> LuaThreadStateAcquire(lua_State *);
// Return the previously acquired Lua thread state.
void LuaThreadStateRelease(LuaThreadState *);

struct ScopedLuaState
{
  explicit ScopedLuaState(instanceid_t instanceid)
    : ptr(LuaThreadStateAcquire(instanceid)) {
  }

  explicit ScopedLuaState(lua_State * lua)
    : ptr(LuaThreadStateAcquire(lua)) {
  }

  ~ScopedLuaState() {
    LuaThreadStateRelease(this->ptr.first);
  }

  operator bool() const {
    return this->ptr.first != NULL;
  }

  LuaThreadState * operator->() const {
    return this->ptr.first;
  }

  LuaPluginInstance * instance() const {
    return this->ptr.second;
  }

private:
  std::pair<LuaThreadState *, LuaPluginInstance *> ptr;
};

#endif // LUA_STATE_H_
