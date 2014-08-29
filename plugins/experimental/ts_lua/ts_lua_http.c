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
#include "ts_lua_http_intercept.h"
#include "ts_lua_http_config.h"
#include "ts_lua_http_cntl.h"

typedef enum
{
  TS_LUA_CACHE_LOOKUP_MISS,
  TS_LUA_CACHE_LOOKUP_HIT_STALE,
  TS_LUA_CACHE_LOOKUP_HIT_FRESH,
  TS_LUA_CACHE_LOOKUP_SKIPPED
} TSLuaCacheLookupResult;

char *ts_lua_cache_lookup_result_string[] = {
  "TS_LUA_CACHE_LOOKUP_MISS",
  "TS_LUA_CACHE_LOOKUP_HIT_STALE",
  "TS_LUA_CACHE_LOOKUP_HIT_FRESH",
  "TS_LUA_CACHE_LOOKUP_SKIPPED"
};

static void ts_lua_inject_http_retset_api(lua_State * L);
static void ts_lua_inject_http_cache_api(lua_State * L);
static void ts_lua_inject_http_transform_api(lua_State * L);
static void ts_lua_inject_http_misc_api(lua_State * L);

static int ts_lua_http_set_retstatus(lua_State * L);
static int ts_lua_http_set_retbody(lua_State * L);
static int ts_lua_http_set_resp(lua_State * L);

static int ts_lua_http_get_cache_lookup_status(lua_State * L);
static int ts_lua_http_set_cache_url(lua_State * L);

static void ts_lua_inject_cache_lookup_result_variables(lua_State * L);

static int ts_lua_http_resp_cache_transformed(lua_State * L);
static int ts_lua_http_resp_cache_untransformed(lua_State * L);

static int ts_lua_http_is_internal_request(lua_State * L);
static int ts_lua_http_skip_remapping_set(lua_State * L);

void
ts_lua_inject_http_api(lua_State * L)
{
  lua_newtable(L);

  ts_lua_inject_http_retset_api(L);
  ts_lua_inject_http_cache_api(L);
  ts_lua_inject_http_transform_api(L);
  ts_lua_inject_http_intercept_api(L);
  ts_lua_inject_http_config_api(L);
  ts_lua_inject_http_cntl_api(L);
  ts_lua_inject_http_misc_api(L);

  lua_setfield(L, -2, "http");
}

static void
ts_lua_inject_http_retset_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_http_set_retstatus);
  lua_setfield(L, -2, "set_retstatus");

  lua_pushcfunction(L, ts_lua_http_set_retbody);
  lua_setfield(L, -2, "set_retbody");

  lua_pushcfunction(L, ts_lua_http_set_resp);
  lua_setfield(L, -2, "set_resp");
}

static void
ts_lua_inject_http_cache_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_http_get_cache_lookup_status);
  lua_setfield(L, -2, "get_cache_lookup_status");

  lua_pushcfunction(L, ts_lua_http_set_cache_url);
  lua_setfield(L, -2, "set_cache_url");

  ts_lua_inject_cache_lookup_result_variables(L);
}

static void
ts_lua_inject_http_transform_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_http_resp_cache_transformed);
  lua_setfield(L, -2, "resp_cache_transformed");

  lua_pushcfunction(L, ts_lua_http_resp_cache_untransformed);
  lua_setfield(L, -2, "resp_cache_untransformed");
}

static void
ts_lua_inject_http_misc_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_http_is_internal_request);
  lua_setfield(L, -2, "is_internal_request");

  lua_pushcfunction(L, ts_lua_http_skip_remapping_set);
  lua_setfield(L, -2, "skip_remapping_set");
}

static void
ts_lua_inject_cache_lookup_result_variables(lua_State * L)
{
  int i;

  for (i = 0; i < sizeof(ts_lua_cache_lookup_result_string) / sizeof(char *); i++) {
    lua_pushinteger(L, i);
    lua_setglobal(L, ts_lua_cache_lookup_result_string[i]);
  }
}

static int
ts_lua_http_set_retstatus(lua_State * L)
{
  int status;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  status = luaL_checkinteger(L, 1);
  TSHttpTxnSetHttpRetStatus(http_ctx->txnp, status);
  return 0;
}

static int
ts_lua_http_set_retbody(lua_State * L)
{
  const char *body;
  size_t body_len;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  body = luaL_checklstring(L, 1, &body_len);
  TSHttpTxnErrorBodySet(http_ctx->txnp, TSstrdup(body), body_len, NULL);        // Defaults to text/html
  return 0;
}

static int
ts_lua_http_set_resp(lua_State * L)
{
  int n, status;
  const char *body;
  size_t body_len;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  n = lua_gettop(L);

  status = luaL_checkinteger(L, 1);
  TSHttpTxnSetHttpRetStatus(http_ctx->txnp, status);

  if (n == 2) {
    body = luaL_checklstring(L, 2, &body_len);
    TSHttpTxnErrorBodySet(http_ctx->txnp, TSstrdup(body), body_len, NULL);      // Defaults to text/html
  }

  return 0;
}

static int
ts_lua_http_get_cache_lookup_status(lua_State * L)
{
  int status;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  if (TSHttpTxnCacheLookupStatusGet(http_ctx->txnp, &status) == TS_ERROR) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, status);
  }

  return 1;
}

static int
ts_lua_http_set_cache_url(lua_State * L)
{
  const char *url;
  size_t url_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  url = luaL_checklstring(L, 1, &url_len);

  if (url && url_len) {
    TSCacheUrlSet(http_ctx->txnp, url, url_len);
  }

  return 0;
}

static int
ts_lua_http_resp_cache_transformed(lua_State * L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  action = luaL_checkinteger(L, 1);

  TSHttpTxnTransformedRespCache(http_ctx->txnp, action);

  return 0;
}

static int
ts_lua_http_resp_cache_untransformed(lua_State * L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  action = luaL_checkinteger(L, 1);

  TSHttpTxnUntransformedRespCache(http_ctx->txnp, action);

  return 0;
}

static int
ts_lua_http_is_internal_request(lua_State * L)
{
  TSReturnCode ret;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  ret = TSHttpIsInternalRequest(http_ctx->txnp);

  if (ret == TS_SUCCESS) {
    lua_pushnumber(L, 1);

  } else {
    lua_pushnumber(L, 0);
  }

  return 1;
}

static int
ts_lua_http_skip_remapping_set(lua_State * L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  action = luaL_checkinteger(L, 1);

  TSSkipRemappingSet(http_ctx->txnp, action);

  return 0;
}

