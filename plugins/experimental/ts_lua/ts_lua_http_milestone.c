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
  TS_LUA_MILESTONE_UA_BEGIN                = TS_MILESTONE_UA_BEGIN,
  TS_LUA_MILESTONE_UA_FIRST_READ           = TS_MILESTONE_UA_FIRST_READ,
  TS_LUA_MILESTONE_UA_READ_HEADER_DONE     = TS_MILESTONE_UA_READ_HEADER_DONE,
  TS_LUA_MILESTONE_UA_BEGIN_WRITE          = TS_MILESTONE_UA_BEGIN_WRITE,
  TS_LUA_MILESTONE_UA_CLOSE                = TS_MILESTONE_UA_CLOSE,
  TS_LUA_MILESTONE_SERVER_FIRST_CONNECT    = TS_MILESTONE_SERVER_FIRST_CONNECT,
  TS_LUA_MILESTONE_SERVER_CONNECT          = TS_MILESTONE_SERVER_CONNECT,
  TS_LUA_MILESTONE_SERVER_CONNECT_END      = TS_MILESTONE_SERVER_CONNECT_END,
  TS_LUA_MILESTONE_SERVER_BEGIN_WRITE      = TS_MILESTONE_SERVER_BEGIN_WRITE,
  TS_LUA_MILESTONE_SERVER_FIRST_READ       = TS_MILESTONE_SERVER_FIRST_READ,
  TS_LUA_MILESTONE_SERVER_READ_HEADER_DONE = TS_MILESTONE_SERVER_READ_HEADER_DONE,
  TS_LUA_MILESTONE_SERVER_CLOSE            = TS_MILESTONE_SERVER_CLOSE,
  TS_LUA_MILESTONE_CACHE_OPEN_READ_BEGIN   = TS_MILESTONE_CACHE_OPEN_READ_BEGIN,
  TS_LUA_MILESTONE_CACHE_OPEN_READ_END     = TS_MILESTONE_CACHE_OPEN_READ_END,
  TS_LUA_MILESTONE_CACHE_OPEN_WRITE_BEGIN  = TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN,
  TS_LUA_MILESTONE_CACHE_OPEN_WRITE_END    = TS_MILESTONE_CACHE_OPEN_WRITE_END,
  TS_LUA_MILESTONE_DNS_LOOKUP_BEGIN        = TS_MILESTONE_DNS_LOOKUP_BEGIN,
  TS_LUA_MILESTONE_DNS_LOOKUP_END          = TS_MILESTONE_DNS_LOOKUP_END,
  TS_LUA_MILESTONE_SM_START                = TS_MILESTONE_SM_START,
  TS_LUA_MILESTONE_SM_FINISH               = TS_MILESTONE_SM_FINISH,
  TS_LUA_MILESTONE_PLUGIN_ACTIVE           = TS_MILESTONE_PLUGIN_ACTIVE,
  TS_LUA_MILESTONE_PLUGIN_TOTAL            = TS_MILESTONE_PLUGIN_TOTAL
} TSLuaMilestoneType;

ts_lua_var_item ts_lua_milestone_type_vars[] = {TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_UA_BEGIN),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_UA_FIRST_READ),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_UA_READ_HEADER_DONE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_UA_BEGIN_WRITE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_UA_CLOSE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_FIRST_CONNECT),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_CONNECT),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_CONNECT_END),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_BEGIN_WRITE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_FIRST_READ),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_READ_HEADER_DONE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SERVER_CLOSE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_CACHE_OPEN_READ_BEGIN),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_CACHE_OPEN_READ_END),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_CACHE_OPEN_WRITE_BEGIN),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_CACHE_OPEN_WRITE_END),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_DNS_LOOKUP_BEGIN),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_DNS_LOOKUP_END),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SM_START),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_SM_FINISH),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_PLUGIN_ACTIVE),
                                                TS_LUA_MAKE_VAR_ITEM(TS_LUA_MILESTONE_PLUGIN_TOTAL)};

static void ts_lua_inject_http_milestone_variables(lua_State *L);

static int ts_lua_http_milestone_get(lua_State *L);

void
ts_lua_inject_http_milestone_api(lua_State *L)
{
  ts_lua_inject_http_milestone_variables(L);

  lua_pushcfunction(L, ts_lua_http_milestone_get);
  lua_setfield(L, -2, "milestone_get");
}

static void
ts_lua_inject_http_milestone_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_milestone_type_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_milestone_type_vars[i].nvar);
    lua_setglobal(L, ts_lua_milestone_type_vars[i].svar);
  }
}

static int
ts_lua_http_milestone_get(lua_State *L)
{
  int milestone_type;
  TSHRTime value;
  TSHRTime epoch;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  milestone_type = luaL_checkinteger(L, 1);

  if (TS_SUCCESS == TSHttpTxnMilestoneGet(http_ctx->txnp, TS_MILESTONE_SM_START, &epoch)) {
    if (TS_SUCCESS == TSHttpTxnMilestoneGet(http_ctx->txnp, milestone_type, &value)) {
      lua_pushnumber(L, (double)(value - epoch) / 1000000000);
      return 1;
    }
  }

  return 0;
}
