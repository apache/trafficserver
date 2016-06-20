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
#include "ts_lua_http_milestone.h"

typedef enum {
  TS_LUA_CACHE_LOOKUP_MISS,
  TS_LUA_CACHE_LOOKUP_HIT_STALE,
  TS_LUA_CACHE_LOOKUP_HIT_FRESH,
  TS_LUA_CACHE_LOOKUP_SKIPPED
} TSLuaCacheLookupResult;

char *ts_lua_cache_lookup_result_string[] = {"TS_LUA_CACHE_LOOKUP_MISS", "TS_LUA_CACHE_LOOKUP_HIT_STALE",
                                             "TS_LUA_CACHE_LOOKUP_HIT_FRESH", "TS_LUA_CACHE_LOOKUP_SKIPPED"};

static void ts_lua_inject_http_retset_api(lua_State *L);
static void ts_lua_inject_http_cache_api(lua_State *L);
static void ts_lua_inject_http_transform_api(lua_State *L);
static void ts_lua_inject_http_misc_api(lua_State *L);

static int ts_lua_http_set_retstatus(lua_State *L);
static int ts_lua_http_set_retbody(lua_State *L);
static int ts_lua_http_set_resp(lua_State *L);

static int ts_lua_http_get_cache_lookup_status(lua_State *L);
static int ts_lua_http_set_cache_lookup_status(lua_State *L);
static int ts_lua_http_set_cache_url(lua_State *L);
static int ts_lua_http_get_cache_lookup_url(lua_State *L);
static int ts_lua_http_set_cache_lookup_url(lua_State *L);
static int ts_lua_http_set_server_resp_no_store(lua_State *L);

static void ts_lua_inject_cache_lookup_result_variables(lua_State *L);

static int ts_lua_http_resp_cache_transformed(lua_State *L);
static int ts_lua_http_resp_cache_untransformed(lua_State *L);

static int ts_lua_http_is_internal_request(lua_State *L);
static int ts_lua_http_skip_remapping_set(lua_State *L);
static int ts_lua_http_transaction_count(lua_State *L);

static void ts_lua_inject_http_resp_transform_api(lua_State *L);
static int ts_lua_http_resp_transform_get_upstream_bytes(lua_State *L);
static int ts_lua_http_resp_transform_set_downstream_bytes(lua_State *L);

void
ts_lua_inject_http_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_http_retset_api(L);
  ts_lua_inject_http_cache_api(L);
  ts_lua_inject_http_transform_api(L);
  ts_lua_inject_http_intercept_api(L);
  ts_lua_inject_http_config_api(L);
  ts_lua_inject_http_cntl_api(L);
  ts_lua_inject_http_milestone_api(L);
  ts_lua_inject_http_misc_api(L);

  lua_setfield(L, -2, "http");
}

static void
ts_lua_inject_http_retset_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_http_set_retstatus);
  lua_setfield(L, -2, "set_retstatus");

  lua_pushcfunction(L, ts_lua_http_set_retbody);
  lua_setfield(L, -2, "set_retbody");

  lua_pushcfunction(L, ts_lua_http_set_resp);
  lua_setfield(L, -2, "set_resp");
}

static void
ts_lua_inject_http_cache_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_http_get_cache_lookup_status);
  lua_setfield(L, -2, "get_cache_lookup_status");

  lua_pushcfunction(L, ts_lua_http_set_cache_lookup_status);
  lua_setfield(L, -2, "set_cache_lookup_status");

  lua_pushcfunction(L, ts_lua_http_set_cache_url);
  lua_setfield(L, -2, "set_cache_url");

  lua_pushcfunction(L, ts_lua_http_get_cache_lookup_url);
  lua_setfield(L, -2, "get_cache_lookup_url");

  lua_pushcfunction(L, ts_lua_http_set_cache_lookup_url);
  lua_setfield(L, -2, "set_cache_lookup_url");

  lua_pushcfunction(L, ts_lua_http_set_server_resp_no_store);
  lua_setfield(L, -2, "set_server_resp_no_store");

  ts_lua_inject_cache_lookup_result_variables(L);
}

static void
ts_lua_inject_http_transform_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_http_resp_cache_transformed);
  lua_setfield(L, -2, "resp_cache_transformed");

  lua_pushcfunction(L, ts_lua_http_resp_cache_untransformed);
  lua_setfield(L, -2, "resp_cache_untransformed");

  /*  ts.http.resp_transform api */
  lua_newtable(L);
  ts_lua_inject_http_resp_transform_api(L);
  lua_setfield(L, -2, "resp_transform");
}

static void
ts_lua_inject_http_resp_transform_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_http_resp_transform_get_upstream_bytes);
  lua_setfield(L, -2, "get_upstream_bytes");

  lua_pushcfunction(L, ts_lua_http_resp_transform_set_downstream_bytes);
  lua_setfield(L, -2, "set_downstream_bytes");
}

static void
ts_lua_inject_http_misc_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_http_is_internal_request);
  lua_setfield(L, -2, "is_internal_request");

  lua_pushcfunction(L, ts_lua_http_skip_remapping_set);
  lua_setfield(L, -2, "skip_remapping_set");

  lua_pushcfunction(L, ts_lua_http_transaction_count);
  lua_setfield(L, -2, "transaction_count");
}

static void
ts_lua_inject_cache_lookup_result_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_cache_lookup_result_string) / sizeof(char *); i++) {
    lua_pushinteger(L, (lua_Integer)i);
    lua_setglobal(L, ts_lua_cache_lookup_result_string[i]);
  }
}

static int
ts_lua_http_set_retstatus(lua_State *L)
{
  int status;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  status = luaL_checkinteger(L, 1);
  TSHttpTxnSetHttpRetStatus(http_ctx->txnp, status);
  return 0;
}

static int
ts_lua_http_set_retbody(lua_State *L)
{
  const char *body;
  size_t body_len;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  body = luaL_checklstring(L, 1, &body_len);
  TSHttpTxnErrorBodySet(http_ctx->txnp, TSstrdup(body), body_len, NULL); // Defaults to text/html
  return 0;
}

static int
ts_lua_http_set_resp(lua_State *L)
{
  int n, status;
  const char *body;
  size_t body_len;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  n = lua_gettop(L);

  status = luaL_checkinteger(L, 1);
  TSHttpTxnSetHttpRetStatus(http_ctx->txnp, status);

  if (n == 2) {
    body = luaL_checklstring(L, 2, &body_len);
    TSHttpTxnErrorBodySet(http_ctx->txnp, TSstrdup(body), body_len, NULL); // Defaults to text/html
  }

  return 0;
}

static int
ts_lua_http_get_cache_lookup_status(lua_State *L)
{
  int status;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (TSHttpTxnCacheLookupStatusGet(http_ctx->txnp, &status) == TS_ERROR) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, status);
  }

  return 1;
}

static int
ts_lua_http_set_cache_lookup_status(lua_State *L)
{
  int status;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  status = luaL_checknumber(L, 1);

  TSHttpTxnCacheLookupStatusSet(http_ctx->txnp, status);

  return 0;
}

static int
ts_lua_http_get_cache_lookup_url(lua_State *L)
{
  char output[TS_LUA_MAX_URL_LENGTH];
  int output_len;
  TSMLoc url = TS_NULL_MLOC;
  char *str  = NULL;
  int len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (TSUrlCreate(http_ctx->client_request_bufp, &url) != TS_SUCCESS) {
    lua_pushnil(L);
    goto done;
  }

  if (TSHttpTxnCacheLookupUrlGet(http_ctx->txnp, http_ctx->client_request_bufp, url) != TS_SUCCESS) {
    lua_pushnil(L);
    goto done;
  }

  str = TSUrlStringGet(http_ctx->client_request_bufp, url, &len);

  output_len = snprintf(output, TS_LUA_MAX_URL_LENGTH, "%.*s", len, str);
  if (output_len >= TS_LUA_MAX_URL_LENGTH) {
    lua_pushlstring(L, output, TS_LUA_MAX_URL_LENGTH - 1);
  } else {
    lua_pushlstring(L, output, output_len);
  }

done:
  if (url != TS_NULL_MLOC) {
    TSHandleMLocRelease(http_ctx->client_request_bufp, TS_NULL_MLOC, url);
  }

  if (str != NULL) {
    TSfree(str);
  }

  return 1;
}

static int
ts_lua_http_set_cache_lookup_url(lua_State *L)
{
  const char *url;
  size_t url_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  url = luaL_checklstring(L, 1, &url_len);

  if (url && url_len) {
    const char *start = url;
    const char *end   = url + url_len;
    TSMLoc new_url_loc;
    if (TSUrlCreate(http_ctx->client_request_bufp, &new_url_loc) == TS_SUCCESS &&
        TSUrlParse(http_ctx->client_request_bufp, new_url_loc, &start, end) == TS_PARSE_DONE &&
        TSHttpTxnCacheLookupUrlSet(http_ctx->txnp, http_ctx->client_request_bufp, new_url_loc) == TS_SUCCESS) {
      TSDebug(TS_LUA_DEBUG_TAG, "Set cache lookup URL");
    } else {
      TSError("[ts_lua] Failed to set cache lookup URL");
    }
  }

  return 0;
}

static int
ts_lua_http_set_cache_url(lua_State *L)
{
  const char *url;
  size_t url_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  url = luaL_checklstring(L, 1, &url_len);

  if (url && url_len) {
    if (TSCacheUrlSet(http_ctx->txnp, url, url_len) != TS_SUCCESS) {
      TSError("[ts_lua] Failed to set cache url");
    }
  }

  return 0;
}

static int
ts_lua_http_set_server_resp_no_store(lua_State *L)
{
  int status;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  status = luaL_checknumber(L, 1);

  TSHttpTxnServerRespNoStoreSet(http_ctx->txnp, status);

  return 0;
}

static int
ts_lua_http_resp_cache_transformed(lua_State *L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  action = luaL_checkinteger(L, 1);

  TSHttpTxnTransformedRespCache(http_ctx->txnp, action);

  return 0;
}

static int
ts_lua_http_resp_cache_untransformed(lua_State *L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  action = luaL_checkinteger(L, 1);

  TSHttpTxnUntransformedRespCache(http_ctx->txnp, action);

  return 0;
}

static int
ts_lua_http_is_internal_request(lua_State *L)
{
  TSReturnCode ret;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  ret = TSHttpTxnIsInternal(http_ctx->txnp);

  if (ret == TS_SUCCESS) {
    lua_pushnumber(L, 1);

  } else {
    lua_pushnumber(L, 0);
  }

  return 1;
}

static int
ts_lua_http_skip_remapping_set(lua_State *L)
{
  int action;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  action = luaL_checkinteger(L, 1);

  TSSkipRemappingSet(http_ctx->txnp, action);

  return 0;
}

static int
ts_lua_http_transaction_count(lua_State *L)
{
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TSHttpSsn ssn = TSHttpTxnSsnGet(http_ctx->txnp);
  if (ssn) {
    int n = TSHttpSsnTransactionCount(ssn);
    lua_pushnumber(L, n);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_http_resp_transform_get_upstream_bytes(lua_State *L)
{
  ts_lua_http_transform_ctx *transform_ctx;

  transform_ctx = ts_lua_get_http_transform_ctx(L);
  if (transform_ctx == NULL) {
    TSError("[ts_lua] missing transform_ctx");
    return 0;
  }

  lua_pushnumber(L, transform_ctx->upstream_bytes);

  return 1;
}

static int
ts_lua_http_resp_transform_set_downstream_bytes(lua_State *L)
{
  int64_t n;
  ts_lua_http_transform_ctx *transform_ctx;

  transform_ctx = ts_lua_get_http_transform_ctx(L);
  if (transform_ctx == NULL) {
    TSError("[ts_lua] missing transform_ctx");
    return 0;
  }

  n = luaL_checkinteger(L, 1);

  transform_ctx->downstream_bytes = n;

  return 0;
}
