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

typedef enum {
  TS_LUA_STAT_PERSISTENT     = TS_STAT_PERSISTENT,
  TS_LUA_STAT_NON_PERSISTENT = TS_STAT_NON_PERSISTENT
} TSLuaStatPersistentType;

ts_lua_var_item ts_lua_stat_persistent_vars[] = {TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_PERSISTENT),
                                                 TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_NON_PERSISTENT)};

typedef enum {
  TS_LUA_STAT_SYNC_SUM     = TS_STAT_SYNC_SUM,
  TS_LUA_STAT_SYNC_COUNT   = TS_STAT_SYNC_COUNT,
  TS_LUA_STAT_SYNC_AVG     = TS_STAT_SYNC_AVG,
  TS_LUA_STAT_SYNC_TIMEAVG = TS_STAT_SYNC_TIMEAVG
} TSLuaStatSyncType;

ts_lua_var_item ts_lua_stat_sync_vars[] = {TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_SYNC_SUM), TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_SYNC_COUNT),
                                           TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_SYNC_AVG),
                                           TS_LUA_MAKE_VAR_ITEM(TS_LUA_STAT_SYNC_TIMEAVG)};

typedef enum {
  TS_LUA_RECORDDATATYPE_INT = TS_RECORDDATATYPE_INT,
} TSLuaStatRecordType;

ts_lua_var_item ts_lua_stat_record_vars[] = {TS_LUA_MAKE_VAR_ITEM(TS_LUA_RECORDDATATYPE_INT)};

static void ts_lua_inject_stat_variables(lua_State *L);

static int ts_lua_stat_create(lua_State *L);
static int ts_lua_stat_find(lua_State *L);

static int ts_lua_stat_increment(lua_State *L);
static int ts_lua_stat_decrement(lua_State *L);
static int ts_lua_stat_get_value(lua_State *L);
static int ts_lua_stat_set_value(lua_State *L);

void
ts_lua_inject_stat_api(lua_State *L)
{
  ts_lua_inject_stat_variables(L);

  lua_pushcfunction(L, ts_lua_stat_create);
  lua_setfield(L, -2, "stat_create");

  lua_pushcfunction(L, ts_lua_stat_find);
  lua_setfield(L, -2, "stat_find");
}

static void
ts_lua_inject_stat_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_stat_persistent_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_stat_persistent_vars[i].nvar);
    lua_setglobal(L, ts_lua_stat_persistent_vars[i].svar);
  }

  for (i = 0; i < sizeof(ts_lua_stat_sync_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_stat_sync_vars[i].nvar);
    lua_setglobal(L, ts_lua_stat_sync_vars[i].svar);
  }

  for (i = 0; i < sizeof(ts_lua_stat_record_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_stat_record_vars[i].nvar);
    lua_setglobal(L, ts_lua_stat_record_vars[i].svar);
  }
}

static int
ts_lua_stat_create(lua_State *L)
{
  const char *name;
  size_t name_len;
  int type;
  int persist;
  int sync;
  int idp;

  name = luaL_checklstring(L, 1, &name_len);

  if (lua_isnil(L, 2)) {
    type = TS_RECORDDATATYPE_INT;
  } else {
    type = luaL_checkinteger(L, 2);
  }

  if (lua_isnil(L, 3)) {
    persist = TS_STAT_PERSISTENT;
  } else {
    persist = luaL_checkinteger(L, 3);
  }

  if (lua_isnil(L, 4)) {
    sync = TS_STAT_SYNC_SUM;
  } else {
    sync = luaL_checkinteger(L, 4);
  }

  if (name && name_len) {
    if (TSStatFindName(name, &idp) == TS_ERROR) {
      idp = TSStatCreate(name, type, persist, sync);
    }

    lua_newtable(L);
    lua_pushnumber(L, idp);
    lua_setfield(L, -2, "id");

    lua_pushcfunction(L, ts_lua_stat_increment);
    lua_setfield(L, -2, "increment");
    lua_pushcfunction(L, ts_lua_stat_decrement);
    lua_setfield(L, -2, "decrement");
    lua_pushcfunction(L, ts_lua_stat_get_value);
    lua_setfield(L, -2, "get_value");
    lua_pushcfunction(L, ts_lua_stat_set_value);
    lua_setfield(L, -2, "set_value");
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_stat_find(lua_State *L)
{
  const char *name;
  size_t name_len;
  int idp;

  name = luaL_checklstring(L, 1, &name_len);

  if (name && name_len) {
    if (TSStatFindName(name, &idp) != TS_ERROR) {
      lua_newtable(L);
      lua_pushnumber(L, idp);
      lua_setfield(L, -2, "id");

      lua_pushcfunction(L, ts_lua_stat_increment);
      lua_setfield(L, -2, "increment");
      lua_pushcfunction(L, ts_lua_stat_decrement);
      lua_setfield(L, -2, "decrement");
      lua_pushcfunction(L, ts_lua_stat_get_value);
      lua_setfield(L, -2, "get_value");
      lua_pushcfunction(L, ts_lua_stat_set_value);
      lua_setfield(L, -2, "set_value");
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_stat_increment(lua_State *L)
{
  int increment;
  int idp;
  increment = luaL_checkinteger(L, 2);

  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, -2, "id");
  idp = luaL_checknumber(L, -1);
  lua_pop(L, 1);

  TSStatIntIncrement(idp, increment);

  return 0;
}

static int
ts_lua_stat_decrement(lua_State *L)
{
  int decrement;
  int idp;
  decrement = luaL_checkinteger(L, 2);

  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, -2, "id");
  idp = luaL_checknumber(L, -1);
  lua_pop(L, 1);

  TSStatIntDecrement(idp, decrement);

  return 0;
}

static int
ts_lua_stat_get_value(lua_State *L)
{
  int value;
  int idp;

  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, -1, "id");
  idp = luaL_checknumber(L, -1);
  lua_pop(L, 1);

  value = TSStatIntGet(idp);

  lua_pushnumber(L, value);
  return 1;
}

static int
ts_lua_stat_set_value(lua_State *L)
{
  int value;
  int idp;
  value = luaL_checkinteger(L, 2);

  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, -2, "id");
  idp = luaL_checknumber(L, -1);
  lua_pop(L, 1);

  TSStatIntSet(idp, value);

  return 0;
}
