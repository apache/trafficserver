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
  TS_LUA_TXN_INFO_CACHE_HIT_RAM           = TS_TXN_INFO_CACHE_HIT_RAM,
  TS_LUA_TXN_INFO_CACHE_COMPRESSED_IN_RAM = TS_TXN_INFO_CACHE_COMPRESSED_IN_RAM,
  TS_LUA_TXN_INFO_CACHE_HIT_RWW           = TS_TXN_INFO_CACHE_HIT_RWW,
  TS_LUA_TXN_INFO_CACHE_OPEN_READ_TRIES   = TS_TXN_INFO_CACHE_OPEN_READ_TRIES,
  TS_LUA_TXN_INFO_CACHE_OPEN_WRITE_TRIES  = TS_TXN_INFO_CACHE_OPEN_WRITE_TRIES,
  TS_LUA_TXN_INFO_CACHE_VOLUME            = TS_TXN_INFO_CACHE_VOLUME
} TSLuaTxnInfoType;

ts_lua_var_item ts_lua_txn_info_type_vars[] = {
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_HIT_RAM),          TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_COMPRESSED_IN_RAM),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_HIT_RWW),          TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_OPEN_READ_TRIES),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_OPEN_WRITE_TRIES), TS_LUA_MAKE_VAR_ITEM(TS_LUA_TXN_INFO_CACHE_VOLUME)};

static void ts_lua_inject_txn_info_variables(lua_State *L);

static int ts_lua_txn_info_get(lua_State *L);

void
ts_lua_inject_txn_info_api(lua_State *L)
{
  ts_lua_inject_txn_info_variables(L);

  lua_pushcfunction(L, ts_lua_txn_info_get);
  lua_setfield(L, -2, "txn_info_get");
}

static void
ts_lua_inject_txn_info_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_txn_info_type_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_txn_info_type_vars[i].nvar);
    lua_setglobal(L, ts_lua_txn_info_type_vars[i].svar);
  }
}

static int
ts_lua_txn_info_get(lua_State *L)
{
  TSHttpTxnInfoKey type;
  TSMgmtInt value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  type = luaL_checkinteger(L, 1);

  if (TS_SUCCESS == TSHttpTxnInfoIntGet(http_ctx->txnp, type, &value)) {
    lua_pushnumber(L, value);
    return 1;
  }

  return 0;
}
