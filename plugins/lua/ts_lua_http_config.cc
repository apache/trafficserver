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

#include "ts/apidefs.h"
#include "ts_lua_util.h"
#include "proxy/http/OverridableConfigDefs.h"

// Generate the Lua config enum from the X-macro.
// Each entry maps TS_LUA_CONFIG_<KEY> = TS_CONFIG_<KEY>.
typedef enum {
#define X_LUA_ENUM(CONFIG_KEY, MEMBER, RECORD_NAME, DATA_TYPE, CONV) TS_LUA_CONFIG_##CONFIG_KEY = TS_CONFIG_##CONFIG_KEY,
  OVERRIDABLE_CONFIGS(X_LUA_ENUM)
#undef X_LUA_ENUM

    TS_LUA_CONFIG_LAST_ENTRY = TS_CONFIG_LAST_ENTRY,
} TSLuaOverridableConfigKey;

typedef enum {
  TS_LUA_TIMEOUT_ACTIVE      = 0,
  TS_LUA_TIMEOUT_CONNECT     = 1,
  TS_LUA_TIMEOUT_DNS         = 2,
  TS_LUA_TIMEOUT_NO_ACTIVITY = 3
} TSLuaTimeoutKey;

// Generate the Lua config variable array from the X-macro.
// The 5th parameter (CONV) is ignored here.
ts_lua_var_item ts_lua_http_config_vars[] = {
#define X_LUA_VAR(CONFIG_KEY, MEMBER, RECORD_NAME, DATA_TYPE, CONV) TS_LUA_MAKE_VAR_ITEM(TS_LUA_CONFIG_##CONFIG_KEY),
  OVERRIDABLE_CONFIGS(X_LUA_VAR)
#undef X_LUA_VAR

    TS_LUA_MAKE_VAR_ITEM(TS_LUA_CONFIG_LAST_ENTRY),
};

// Validate that we have the correct number of entries.
#define NUM_HTTP_CONFIG_VARS (sizeof(ts_lua_http_config_vars) / sizeof(ts_lua_http_config_vars[0]))
extern char __ts_lua_http_config_vars_static_assert[NUM_HTTP_CONFIG_VARS == TS_CONFIG_LAST_ENTRY + 1 ? 0 : -1];

ts_lua_var_item ts_lua_http_timeout_vars[] = {
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TIMEOUT_ACTIVE),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TIMEOUT_CONNECT),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TIMEOUT_DNS),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_TIMEOUT_NO_ACTIVITY),
};

static void ts_lua_inject_http_config_variables(lua_State *L);

static int ts_lua_http_config_int_set(lua_State *L);
static int ts_lua_http_config_int_get(lua_State *L);
static int ts_lua_http_config_float_set(lua_State *L);
static int ts_lua_http_config_float_get(lua_State *L);
static int ts_lua_http_config_string_set(lua_State *L);
static int ts_lua_http_config_string_get(lua_State *L);
static int ts_lua_http_timeout_set(lua_State *L);
static int ts_lua_http_client_packet_mark_set(lua_State *L);
static int ts_lua_http_server_packet_mark_set(lua_State *L);
static int ts_lua_http_client_packet_dscp_set(lua_State *L);
static int ts_lua_http_server_packet_dscp_set(lua_State *L);
static int ts_lua_http_enable_redirect(lua_State *L);
static int ts_lua_http_set_debug(lua_State *L);

void
ts_lua_inject_http_config_api(lua_State *L)
{
  ts_lua_inject_http_config_variables(L);

  lua_pushcfunction(L, ts_lua_http_config_int_set);
  lua_setfield(L, -2, "config_int_set");

  lua_pushcfunction(L, ts_lua_http_config_int_get);
  lua_setfield(L, -2, "config_int_get");

  lua_pushcfunction(L, ts_lua_http_config_float_set);
  lua_setfield(L, -2, "config_float_set");

  lua_pushcfunction(L, ts_lua_http_config_float_get);
  lua_setfield(L, -2, "config_float_get");

  lua_pushcfunction(L, ts_lua_http_config_string_set);
  lua_setfield(L, -2, "config_string_set");

  lua_pushcfunction(L, ts_lua_http_config_string_get);
  lua_setfield(L, -2, "config_string_get");

  lua_pushcfunction(L, ts_lua_http_timeout_set);
  lua_setfield(L, -2, "timeout_set");

  lua_pushcfunction(L, ts_lua_http_client_packet_mark_set);
  lua_setfield(L, -2, "client_packet_mark_set");

  lua_pushcfunction(L, ts_lua_http_server_packet_mark_set);
  lua_setfield(L, -2, "server_packet_mark_set");

  lua_pushcfunction(L, ts_lua_http_client_packet_dscp_set);
  lua_setfield(L, -2, "client_packet_dscp_set");

  lua_pushcfunction(L, ts_lua_http_server_packet_dscp_set);
  lua_setfield(L, -2, "server_packet_dscp_set");

  lua_pushcfunction(L, ts_lua_http_enable_redirect);
  lua_setfield(L, -2, "enable_redirect");

  lua_pushcfunction(L, ts_lua_http_set_debug);
  lua_setfield(L, -2, "set_debug");
}

static void
ts_lua_inject_http_config_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_http_config_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_http_config_vars[i].nvar);
    lua_setglobal(L, ts_lua_http_config_vars[i].svar);
  }

  for (i = 0; i < sizeof(ts_lua_http_timeout_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_http_timeout_vars[i].nvar);
    lua_setglobal(L, ts_lua_http_timeout_vars[i].svar);
  }
}

static int
ts_lua_http_config_int_set(lua_State *L)
{
  int              conf;
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf  = luaL_checkinteger(L, 1);
  value = luaL_checkinteger(L, 2);

  TSHttpTxnConfigIntSet(http_ctx->txnp, TSOverridableConfigKey(conf), value);

  return 0;
}

static int
ts_lua_http_config_int_get(lua_State *L)
{
  int              conf;
  int64_t          value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf = luaL_checkinteger(L, 1);

  TSHttpTxnConfigIntGet(http_ctx->txnp, TSOverridableConfigKey(conf), &value);

  lua_pushnumber(L, value);

  return 1;
}

static int
ts_lua_http_config_float_set(lua_State *L)
{
  int              conf;
  float            value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf  = luaL_checkinteger(L, 1);
  value = luaL_checknumber(L, 2);

  TSHttpTxnConfigFloatSet(http_ctx->txnp, TSOverridableConfigKey(conf), value);

  return 0;
}

static int
ts_lua_http_config_float_get(lua_State *L)
{
  int              conf;
  float            value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf = luaL_checkinteger(L, 1);

  TSHttpTxnConfigFloatGet(http_ctx->txnp, TSOverridableConfigKey(conf), &value);

  lua_pushnumber(L, value);

  return 1;
}

static int
ts_lua_http_config_string_set(lua_State *L)
{
  int              conf;
  const char      *value;
  size_t           value_len;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf  = luaL_checkinteger(L, 1);
  value = luaL_checklstring(L, 2, &value_len);

  TSHttpTxnConfigStringSet(http_ctx->txnp, TSOverridableConfigKey(conf), value, value_len);

  return 0;
}

static int
ts_lua_http_config_string_get(lua_State *L)
{
  int              conf;
  const char      *value;
  int              value_len;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf = luaL_checkinteger(L, 1);

  TSHttpTxnConfigStringGet(http_ctx->txnp, TSOverridableConfigKey(conf), &value, &value_len);

  lua_pushlstring(L, value, value_len);

  return 1;
}

static int
ts_lua_http_timeout_set(lua_State *L)
{
  int              conf;
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  conf  = luaL_checkinteger(L, 1);
  value = luaL_checkinteger(L, 2);

  switch (conf) {
  case TS_LUA_TIMEOUT_ACTIVE:
    Dbg(dbg_ctl, "setting active timeout");
    TSHttpTxnActiveTimeoutSet(http_ctx->txnp, value);
    break;

  case TS_LUA_TIMEOUT_CONNECT:
    Dbg(dbg_ctl, "setting connect timeout");
    TSHttpTxnConnectTimeoutSet(http_ctx->txnp, value);
    break;

  case TS_LUA_TIMEOUT_DNS:
    Dbg(dbg_ctl, "setting dns timeout");
    TSHttpTxnDNSTimeoutSet(http_ctx->txnp, value);
    break;

  case TS_LUA_TIMEOUT_NO_ACTIVITY:
    Dbg(dbg_ctl, "setting no activity timeout");
    TSHttpTxnNoActivityTimeoutSet(http_ctx->txnp, value);
    break;

  default:
    TSError("[ts_lua][%s] Unsupported timeout config option for lua plugin", __FUNCTION__);
    break;
  }

  return 0;
}

static int
ts_lua_http_client_packet_mark_set(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "client packet mark set");
  TSHttpTxnClientPacketMarkSet(http_ctx->txnp, value);

  return 0;
}

static int
ts_lua_http_server_packet_mark_set(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "server packet mark set");
  TSHttpTxnServerPacketMarkSet(http_ctx->txnp, value);

  return 0;
}

/* ToDo: This should be removed, it's not needed */
static int
ts_lua_http_enable_redirect(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "enable redirect");
  TSHttpTxnConfigIntSet(http_ctx->txnp, TS_CONFIG_HTTP_NUMBER_OF_REDIRECTIONS, value);

  return 0;
}

static int
ts_lua_http_set_debug(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "set debug");
  TSHttpTxnCntlSet(http_ctx->txnp, TS_HTTP_CNTL_TXN_DEBUG, (value != 0));

  return 0;
}

static int
ts_lua_http_client_packet_dscp_set(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "client packet dscp set");
  TSHttpTxnClientPacketDscpSet(http_ctx->txnp, value);

  return 0;
}

static int
ts_lua_http_server_packet_dscp_set(lua_State *L)
{
  int              value;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  value = luaL_checkinteger(L, 1);

  Dbg(dbg_ctl, "server packet dscp set");
  TSHttpTxnServerPacketDscpSet(http_ctx->txnp, value);

  return 0;
}
