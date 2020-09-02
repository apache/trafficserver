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

static int ts_lua_mgmt_get_int(lua_State *L);
static int ts_lua_mgmt_get_counter(lua_State *L);
static int ts_lua_mgmt_get_float(lua_State *L);
static int ts_lua_mgmt_get_string(lua_State *L);

void
ts_lua_inject_mgmt_api(lua_State *L)
{
  lua_newtable(L);

  lua_pushcfunction(L, ts_lua_mgmt_get_int);
  lua_setfield(L, -2, "get_int");

  lua_pushcfunction(L, ts_lua_mgmt_get_counter);
  lua_setfield(L, -2, "get_counter");

  lua_pushcfunction(L, ts_lua_mgmt_get_float);
  lua_setfield(L, -2, "get_float");

  lua_pushcfunction(L, ts_lua_mgmt_get_string);
  lua_setfield(L, -2, "get_string");

  lua_setfield(L, -2, "mgmt");
}

static int
ts_lua_mgmt_get_int(lua_State *L)
{
  const char *name;
  size_t name_len;
  TSMgmtInt int_val;

  name = luaL_checklstring(L, 1, &name_len);

  if (TS_SUCCESS == TSMgmtIntGet(name, &int_val)) {
    lua_pushinteger(L, int_val);
    return 1;
  }

  return 0;
}

static int
ts_lua_mgmt_get_counter(lua_State *L)
{
  const char *name;
  size_t name_len;
  TSMgmtCounter counter_val;

  name = luaL_checklstring(L, 1, &name_len);
  if (TS_SUCCESS == TSMgmtCounterGet(name, &counter_val)) {
    lua_pushinteger(L, counter_val);
    return 1;
  }

  return 0;
}

static int
ts_lua_mgmt_get_float(lua_State *L)
{
  const char *name;
  size_t name_len;
  TSMgmtFloat float_val;

  name = luaL_checklstring(L, 1, &name_len);
  if (TS_SUCCESS == TSMgmtFloatGet(name, &float_val)) {
    lua_pushnumber(L, float_val);
    return 1;
  }
  return 0;
}

static int
ts_lua_mgmt_get_string(lua_State *L)
{
  const char *name;
  size_t name_len;
  TSMgmtString str_val;

  name = luaL_checklstring(L, 1, &name_len);
  if (TS_SUCCESS == TSMgmtStringGet(name, &str_val)) {
    lua_pushstring(L, str_val);
    TSfree(str_val);
    return 1;
  }

  return 0;
}
