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

#ifndef LUA_LAPI_H_
#define LUA_LAPI_H_

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

struct LuaRemapRequest
{
  TSRemapRequestInfo *  rri;
  TSHttpTxn             txn;
  TSRemapStatus         status;

  static LuaRemapRequest * get(lua_State * lua, int index);
  static LuaRemapRequest * alloc(lua_State * lua);
};

// Initialize the 'ts' module.
int LuaApiInit(lua_State * lua);

// Push a copy of the given URL.
bool LuaPushUrl(lua_State * lua, TSMBuffer buffer, TSMLoc url);

// Push a wrapper object for the given TSRemapRequestInfo.
LuaRemapRequest *
LuaPushRemapRequestInfo(lua_State * lua, TSHttpTxn txn, TSRemapRequestInfo * rri);

#endif // LUA_LAPI_H_
