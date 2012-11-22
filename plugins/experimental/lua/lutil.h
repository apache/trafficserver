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

#ifndef LUA_LUTIL_H_
#define LUA_LUTIL_H_

#include <lua.hpp>
#include <vector>
#include <string>
#include <memory>
#include <pthread.h>


struct LuaPluginState;
struct LuaThreadInstance;

extern LuaPluginState * LuaPlugin;

// Global argument index for TSHttpSsnArgGet and TSHttpTxnArgGet.
extern int LuaHttpArgIndex;

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

#define LuaLogDebug(fmt, ...) do { \
    if (unlikely(TSIsDebugTagSet("lua"))) { \
        TSDebug("lua", "%s: " fmt, __func__, ##__VA_ARGS__); \
    } \
} while (0)

// In DEBUG mode, log errors to the debug channel. This is handy for making Lua runtime
// errors show up on stdout along with the rest of the debug loggin.
#if DEBUG
#define LuaLogError(fmt, ...) LuaLogDebug(fmt, ##__VA_ARGS__)
#else
#define LuaLogError(fmt, ...) TSError(fmt, ##__VA_ARGS__)
#endif

// Return the type name string for the given index.
static inline const char *
ltypeof(lua_State * lua, int index) {
  return lua_typename(lua, lua_type(lua, index));
}

template <typename T> T * tsnew() {
  void * ptr = TSmalloc(sizeof(T));
  return new(ptr) T();
}

template <typename T> void tsdelete(T * ptr) {
  if (ptr) {
    ptr->~T();
    TSfree(ptr);
  }
}

// Allocate an object with lua_newuserdata() and call the default constructor.
template <typename T> T * LuaNewUserData(lua_State * lua) {
  void * ptr = lua_newuserdata(lua, sizeof(T));
  return new(ptr) T();
}

void LuaPushMetatable(lua_State * lua, const char * name, const luaL_Reg * exports);
void LuaLoadLibraries(lua_State * lua);
void LuaRegisterLibrary(lua_State * lua, const char * name, lua_CFunction loader);

// Set the named field in the table on the top of the stack.
void LuaSetConstantField(lua_State * lua, const char * name, int value);
void LuaSetConstantField(lua_State * lua, const char * name, const char * value);

// Get and set the per-thread lua_State.
LuaThreadInstance * LuaGetThreadInstance();
void LuaSetThreadInstance(LuaThreadInstance * lua);

// Allocate a new lua_State.
lua_State * LuaPluginNewState(void);
lua_State * LuaPluginNewState(LuaPluginState * plugin);
bool LuaPluginLoad(lua_State * lua, LuaPluginState * plugin);

// Global Lua plugin state. Used to reconstruct new lua_States.
struct LuaPluginState
{
  typedef std::vector<std::string> pathlist;

  void init(unsigned argc, const char ** argv) {
    for (unsigned i = 0; i < argc; ++i) {
      paths.push_back(argv[i]);
    }
  }

  pathlist paths;
};

// Per-thread lua_State. Used to execute Lua-side code in ethreads.
struct LuaThreadInstance
{
  lua_State * lua;
  int         hooks[TS_HTTP_LAST_HOOK];

  LuaThreadInstance();
  ~LuaThreadInstance();
};

template <typename T, unsigned N> unsigned
countof(const T (&)[N]) {
  return N;
}

template <typename T>
struct thread_local_pointer
{
  thread_local_pointer() {
    TSReleaseAssert(pthread_key_create(&key, NULL) != -1);
  }

  ~thread_local_pointer() {
    pthread_key_delete(key);
  }

  T * get() const {
    return (T *)pthread_getspecific(key);
  }

  void set(T * t) const {
    pthread_setspecific(key, (void *)t);
  }

private:
  pthread_key_t key;
};

#endif // LUA_LUTIL_H_
