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

typedef enum
{
  TS_LUA_HTTP_CNTL_GET_LOGGING_MODE = TS_HTTP_CNTL_GET_LOGGING_MODE,
  TS_LUA_HTTP_CNTL_SET_LOGGING_MODE = TS_HTTP_CNTL_SET_LOGGING_MODE,
  TS_LUA_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE = TS_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE,
  TS_LUA_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE = TS_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE
} TSLuaHttpCntlType;


ts_lua_var_item ts_lua_http_cntl_type_vars[] = {
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_HTTP_CNTL_GET_LOGGING_MODE),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_HTTP_CNTL_SET_LOGGING_MODE),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE)
};


static void ts_lua_inject_http_cntl_variables(lua_State * L);

static int ts_lua_http_cntl_set(lua_State * L);
static int ts_lua_http_cntl_get(lua_State * L);


void
ts_lua_inject_http_cntl_api(lua_State * L)
{
  ts_lua_inject_http_cntl_variables(L);

  lua_pushcfunction(L, ts_lua_http_cntl_set);
  lua_setfield(L, -2, "cntl_set");

  lua_pushcfunction(L, ts_lua_http_cntl_get);
  lua_setfield(L, -2, "cntl_get");
}

static void
ts_lua_inject_http_cntl_variables(lua_State * L)
{
  int i;

  for (i = 0; i < sizeof(ts_lua_http_cntl_type_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_http_cntl_type_vars[i].nvar);
    lua_setglobal(L, ts_lua_http_cntl_type_vars[i].svar);
  }
}

static int
ts_lua_http_cntl_set(lua_State * L)
{
  int cntl_type;
  int value;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  cntl_type = luaL_checkinteger(L, 1);
  value = luaL_checkinteger(L, 2);

  TSHttpTxnCntl(http_ctx->txnp, cntl_type, value ? TS_HTTP_CNTL_ON : TS_HTTP_CNTL_OFF);

  return 0;
}

static int
ts_lua_http_cntl_get(lua_State * L)
{
  int cntl_type;
  int64_t value;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  cntl_type = luaL_checkinteger(L, 1);

  TSHttpTxnCntl(http_ctx->txnp, cntl_type, &value);

  lua_pushnumber(L, value);

  return 1;
}
