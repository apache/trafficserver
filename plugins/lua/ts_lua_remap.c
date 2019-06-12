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
#include "ts_lua_remap.h"

typedef enum {
  TS_LUA_REMAP_NO_REMAP       = TSREMAP_NO_REMAP,
  TS_LUA_REMAP_DID_REMAP      = TSREMAP_DID_REMAP,
  TS_LUA_REMAP_NO_REMAP_STOP  = TSREMAP_NO_REMAP_STOP,
  TS_LUA_REMAP_DID_REMAP_STOP = TSREMAP_DID_REMAP_STOP,
  TS_LUA_REMAP_ERROR          = TSREMAP_ERROR
} TSLuaRemapStatus;

ts_lua_var_item ts_lua_remap_status_vars[] = {
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_REMAP_NO_REMAP), TS_LUA_MAKE_VAR_ITEM(TS_LUA_REMAP_DID_REMAP),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_REMAP_NO_REMAP_STOP), TS_LUA_MAKE_VAR_ITEM(TS_LUA_REMAP_DID_REMAP_STOP),
  TS_LUA_MAKE_VAR_ITEM(TS_LUA_REMAP_ERROR)};

static int ts_lua_remap_get_from_url_host(lua_State *L);
static int ts_lua_remap_get_from_url_port(lua_State *L);
static int ts_lua_remap_get_from_url_scheme(lua_State *L);
static int ts_lua_remap_get_from_uri(lua_State *L);
static int ts_lua_remap_get_from_url(lua_State *L);

static int ts_lua_remap_get_to_url_host(lua_State *L);
static int ts_lua_remap_get_to_url_port(lua_State *L);
static int ts_lua_remap_get_to_url_scheme(lua_State *L);
static int ts_lua_remap_get_to_uri(lua_State *L);
static int ts_lua_remap_get_to_url(lua_State *L);

static void ts_lua_inject_remap_variables(lua_State *L);

void
ts_lua_inject_remap_api(lua_State *L)
{
  ts_lua_inject_remap_variables(L);

  lua_newtable(L);

  lua_pushcfunction(L, ts_lua_remap_get_from_url_host);
  lua_setfield(L, -2, "get_from_url_host");

  lua_pushcfunction(L, ts_lua_remap_get_from_url_port);
  lua_setfield(L, -2, "get_from_url_port");

  lua_pushcfunction(L, ts_lua_remap_get_from_url_scheme);
  lua_setfield(L, -2, "get_from_url_scheme");

  lua_pushcfunction(L, ts_lua_remap_get_from_uri);
  lua_setfield(L, -2, "get_from_uri");

  lua_pushcfunction(L, ts_lua_remap_get_from_url);
  lua_setfield(L, -2, "get_from_url");

  lua_pushcfunction(L, ts_lua_remap_get_to_url_host);
  lua_setfield(L, -2, "get_to_url_host");

  lua_pushcfunction(L, ts_lua_remap_get_to_url_port);
  lua_setfield(L, -2, "get_to_url_port");

  lua_pushcfunction(L, ts_lua_remap_get_to_url_scheme);
  lua_setfield(L, -2, "get_to_url_scheme");

  lua_pushcfunction(L, ts_lua_remap_get_to_uri);
  lua_setfield(L, -2, "get_to_uri");

  lua_pushcfunction(L, ts_lua_remap_get_to_url);
  lua_setfield(L, -2, "get_to_url");

  lua_setfield(L, -2, "remap");
}

static void
ts_lua_inject_remap_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_remap_status_vars) / sizeof(ts_lua_var_item); i++) {
    lua_pushinteger(L, ts_lua_remap_status_vars[i].nvar);
    lua_setglobal(L, ts_lua_remap_status_vars[i].svar);
  }
}

static int
ts_lua_remap_get_from_url_host(lua_State *L)
{
  const char *host;
  int len = 0;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    host = TSUrlHostGet(http_ctx->client_request_bufp, http_ctx->rri->mapFromUrl, &len);

    if (len == 0) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, host, len);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_from_url_port(lua_State *L)
{
  int port;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    port = TSUrlPortGet(http_ctx->client_request_bufp, http_ctx->rri->mapFromUrl);

    lua_pushnumber(L, port);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_from_url_scheme(lua_State *L)
{
  const char *scheme;
  int len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    scheme = TSUrlSchemeGet(http_ctx->client_request_bufp, http_ctx->rri->mapFromUrl, &len);

    if (len == 0) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, scheme, len);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_from_uri(lua_State *L)
{
  const char *path;
  int path_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    path = TSUrlPathGet(http_ctx->client_request_bufp, http_ctx->rri->mapFromUrl, &path_len);

    lua_pushlstring(L, "/", 1);
    lua_pushlstring(L, path, path_len >= TS_LUA_MAX_URL_LENGTH - 1 ? TS_LUA_MAX_URL_LENGTH - 2 : path_len);
    lua_concat(L, 2);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_from_url(lua_State *L)
{
  char *url;
  int url_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    url = TSUrlStringGet(http_ctx->client_request_bufp, http_ctx->rri->mapFromUrl, &url_len);

    lua_pushlstring(L, url, url_len >= TS_LUA_MAX_URL_LENGTH ? TS_LUA_MAX_URL_LENGTH - 1 : url_len);

    TSfree(url);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_to_url_host(lua_State *L)
{
  const char *host;
  int len = 0;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    host = TSUrlHostGet(http_ctx->client_request_bufp, http_ctx->rri->mapToUrl, &len);

    if (len == 0) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, host, len);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_to_url_port(lua_State *L)
{
  int port;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    port = TSUrlPortGet(http_ctx->client_request_bufp, http_ctx->rri->mapToUrl);

    lua_pushnumber(L, port);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_to_url_scheme(lua_State *L)
{
  const char *scheme;
  int len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    scheme = TSUrlSchemeGet(http_ctx->client_request_bufp, http_ctx->rri->mapToUrl, &len);

    if (len == 0) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, scheme, len);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_to_uri(lua_State *L)
{
  const char *path;
  int path_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    path = TSUrlPathGet(http_ctx->client_request_bufp, http_ctx->rri->mapToUrl, &path_len);

    lua_pushlstring(L, "/", 1);
    lua_pushlstring(L, path, path_len >= TS_LUA_MAX_URL_LENGTH - 1 ? TS_LUA_MAX_URL_LENGTH - 2 : path_len);
    lua_concat(L, 2);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_remap_get_to_url(lua_State *L)
{
  char *url;
  int url_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (http_ctx->rri != NULL) {
    url = TSUrlStringGet(http_ctx->client_request_bufp, http_ctx->rri->mapToUrl, &url_len);

    lua_pushlstring(L, url, url_len >= TS_LUA_MAX_URL_LENGTH ? TS_LUA_MAX_URL_LENGTH - 1 : url_len);

    TSfree(url);
  } else {
    lua_pushnil(L);
  }

  return 1;
}
