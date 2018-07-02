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

#include "ts_lua_util.h"

static char ts_http_context_key;

static int ts_lua_context_get(lua_State *L);
static int ts_lua_context_set(lua_State *L);

void
ts_lua_inject_context_api(lua_State *L)
{
  lua_newtable(L); /* .ctx */

  lua_createtable(L, 0, 2); /* metatable for context */

  lua_pushcfunction(L, ts_lua_context_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_context_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "ctx");
}

void
ts_lua_create_context_table(lua_State *L)
{
  lua_pushlightuserdata(L, &ts_http_context_key);
  lua_newtable(L);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

static int
ts_lua_context_get(lua_State *L)
{
  const char *key;
  size_t key_len;

  key = luaL_checklstring(L, 2, &key_len);

  if (key && key_len) {
    lua_pushlightuserdata(L, &ts_http_context_key);
    lua_rawget(L, LUA_GLOBALSINDEX); // get the context table

    lua_pushlstring(L, key, key_len);
    lua_rawget(L, -2);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_context_set(lua_State *L)
{
  const char *key;
  size_t key_len;

  key = luaL_checklstring(L, 2, &key_len);

  lua_pushlightuserdata(L, &ts_http_context_key);
  lua_rawget(L, LUA_GLOBALSINDEX); // get the context table    -3

  lua_pushlstring(L, key, key_len); // push key                 -2
  lua_pushvalue(L, 3);              // push value               -1

  lua_rawset(L, -3);
  lua_pop(L, 1); // pop the context table

  return 0;
}
