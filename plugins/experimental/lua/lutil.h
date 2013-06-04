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
#include "ink_defs.h"

// Global argument index for TSHttpSsnArgGet and TSHttpTxnArgGet.
extern int LuaHttpArgIndex;

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

// Debug log the Lua stack.
void LuaDebugStack(lua_State *);

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

// Allocate a new lua_State.
lua_State * LuaNewState();
lua_State * LuaPluginNewState(void);

#endif // LUA_LUTIL_H_
