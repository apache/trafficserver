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


#include <arpa/inet.h>
#include "ts_lua_util.h"

#define TS_LUA_CHECK_SERVER_REQUEST_HDR(http_ctx)     \
do {        \
    if (!http_ctx->server_request_hdrp) {           \
        if (TSHttpTxnServerReqGet(http_ctx->txnp,   \
                    &http_ctx->server_request_bufp, \
                    &http_ctx->server_request_hdrp) != TS_SUCCESS) {    \
            return 0;   \
        }   \
    }   \
} while(0)


#define TS_LUA_CHECK_SERVER_REQUEST_URL(http_ctx)   \
do {        \
    if (!http_ctx->server_request_url) {            \
        TS_LUA_CHECK_SERVER_REQUEST_HDR(http_ctx);  \
        if (TSHttpHdrUrlGet(http_ctx->server_request_bufp,   \
                    http_ctx->server_request_hdrp, \
                    &http_ctx->server_request_url) != TS_SUCCESS) {    \
            return 0;   \
        }   \
    }   \
} while(0)


static void ts_lua_inject_server_request_header_api(lua_State * L);
static void ts_lua_inject_server_request_headers_api(lua_State * L);
static void ts_lua_inject_server_request_get_header_size_api(lua_State * L);
static void ts_lua_inject_server_request_get_body_size_api(lua_State * L);
static void ts_lua_inject_server_request_uri_api(lua_State * L);
static void ts_lua_inject_server_request_uri_args_api(lua_State * L);

static int ts_lua_server_request_header_get(lua_State * L);
static int ts_lua_server_request_header_set(lua_State * L);
static int ts_lua_server_request_get_headers(lua_State * L);
static int ts_lua_server_request_get_header_size(lua_State * L);
static int ts_lua_server_request_get_body_size(lua_State * L);
static int ts_lua_server_request_get_uri(lua_State * L);
static int ts_lua_server_request_set_uri(lua_State * L);
static int ts_lua_server_request_set_uri_args(lua_State * L);
static int ts_lua_server_request_get_uri_args(lua_State * L);


void
ts_lua_inject_server_request_api(lua_State * L)
{
  lua_newtable(L);

  ts_lua_inject_server_request_header_api(L);
  ts_lua_inject_server_request_headers_api(L);
  ts_lua_inject_server_request_get_header_size_api(L);
  ts_lua_inject_server_request_get_body_size_api(L);

  ts_lua_inject_server_request_uri_api(L);
  ts_lua_inject_server_request_uri_args_api(L);

  lua_setfield(L, -2, "server_request");
}


static void
ts_lua_inject_server_request_header_api(lua_State * L)
{
  lua_newtable(L);              /* .header */

  lua_createtable(L, 0, 2);     /* metatable for .header */

  lua_pushcfunction(L, ts_lua_server_request_header_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_server_request_header_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "header");
}

static int
ts_lua_server_request_header_get(lua_State * L)
{
  const char *key;
  const char *val;
  int val_len;
  size_t key_len;

  TSMLoc field_loc;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  /*  we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);

  if (!http_ctx->server_request_hdrp) {
    if (TSHttpTxnServerReqGet(http_ctx->txnp,
                              &http_ctx->server_request_bufp, &http_ctx->server_request_hdrp) != TS_SUCCESS) {

      lua_pushnil(L);
      return 1;
    }
  }

  if (key && key_len) {

    field_loc = TSMimeHdrFieldFind(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, key, key_len);
    if (field_loc) {
      val =
        TSMimeHdrFieldValueStringGet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc, -1,
                                     &val_len);
      lua_pushlstring(L, val, val_len);
      TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);

    } else {
      lua_pushnil(L);
    }

  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_server_request_header_set(lua_State * L)
{
  const char *key;
  const char *val;
  size_t val_len;
  size_t key_len;
  int remove;

  TSMLoc field_loc;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  remove = 0;
  val = NULL;

  /*   we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);
  if (lua_isnil(L, 3)) {
    remove = 1;
  } else {
    val = luaL_checklstring(L, 3, &val_len);
  }

  if (!http_ctx->server_request_hdrp) {
    if (TSHttpTxnServerReqGet(http_ctx->txnp,
                              &http_ctx->server_request_bufp, &http_ctx->server_request_hdrp) != TS_SUCCESS) {
      return 0;
    }
  }

  field_loc = TSMimeHdrFieldFind(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, key, key_len);

  if (remove) {
    if (field_loc) {
      TSMimeHdrFieldDestroy(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);
    }

  } else if (field_loc) {
    TSMimeHdrFieldValueStringSet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc, 0, val,
                                 val_len);

  } else if (TSMimeHdrFieldCreateNamed(http_ctx->server_request_bufp, http_ctx->server_request_hdrp,
                                       key, key_len, &field_loc) != TS_SUCCESS) {
    TSError("[%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
    return 0;

  } else {
    TSMimeHdrFieldValueStringSet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc, -1, val,
                                 val_len);
    TSMimeHdrFieldAppend(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);
  }

  if (field_loc)
    TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);

  return 0;
}

static void
ts_lua_inject_server_request_headers_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_server_request_get_headers);
  lua_setfield(L, -2, "get_headers");
}

static int
ts_lua_server_request_get_headers(lua_State * L)
{
  const char *name;
  const char *value;
  int name_len;
  int value_len;
  TSMLoc field_loc;
  TSMLoc next_field_loc;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_SERVER_REQUEST_HDR(http_ctx);

  lua_newtable(L);

  field_loc = TSMimeHdrFieldGet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, 0);

  while (field_loc) {

    name = TSMimeHdrFieldNameGet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc, &name_len);
    if (name && name_len) {

      value =
        TSMimeHdrFieldValueStringGet(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc, -1,
                                     &value_len);
      lua_pushlstring(L, name, name_len);
      lua_pushlstring(L, value, value_len);
      lua_rawset(L, -3);
    }

    next_field_loc = TSMimeHdrFieldNext(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);
    TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, field_loc);
    field_loc = next_field_loc;
  }

  return 1;
}

static void
ts_lua_inject_server_request_get_header_size_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_server_request_get_header_size);
  lua_setfield(L, -2, "get_header_size");
}

static int
ts_lua_server_request_get_header_size(lua_State * L)
{
  int header_size;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  header_size = TSHttpTxnServerReqHdrBytesGet(http_ctx->txnp);
  lua_pushnumber(L, header_size);

  return 1;
}

static void
ts_lua_inject_server_request_get_body_size_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_server_request_get_body_size);
  lua_setfield(L, -2, "get_body_size");
}

static int
ts_lua_server_request_get_body_size(lua_State * L)
{
  int64_t body_size;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  body_size = TSHttpTxnServerReqBodyBytesGet(http_ctx->txnp);
  lua_pushnumber(L, body_size);

  return 1;
}

static void
ts_lua_inject_server_request_uri_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_server_request_set_uri);
  lua_setfield(L, -2, "set_uri");

  lua_pushcfunction(L, ts_lua_server_request_get_uri);
  lua_setfield(L, -2, "get_uri");
}

static int
ts_lua_server_request_get_uri(lua_State * L)
{
  char uri[TS_LUA_MAX_URL_LENGTH];
  const char *path;
  int path_len;
  int uri_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_SERVER_REQUEST_URL(http_ctx);

  path = TSUrlPathGet(http_ctx->server_request_bufp, http_ctx->server_request_url, &path_len);

  uri_len = snprintf(uri, TS_LUA_MAX_URL_LENGTH, "/%.*s", path_len, path);

  lua_pushlstring(L, uri, uri_len);

  return 1;
}

static int
ts_lua_server_request_set_uri(lua_State * L)
{
  const char *path;
  size_t path_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_SERVER_REQUEST_URL(http_ctx);

  path = luaL_checklstring(L, 1, &path_len);

  if (*path == '/') {
    path++;
    path_len--;
  }

  TSUrlPathSet(http_ctx->server_request_bufp, http_ctx->server_request_url, path, path_len);

  return 0;
}

static void
ts_lua_inject_server_request_uri_args_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_server_request_set_uri_args);
  lua_setfield(L, -2, "set_uri_args");

  lua_pushcfunction(L, ts_lua_server_request_get_uri_args);
  lua_setfield(L, -2, "get_uri_args");
}

static int
ts_lua_server_request_set_uri_args(lua_State * L)
{
  const char *param;
  size_t param_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_SERVER_REQUEST_URL(http_ctx);

  param = luaL_checklstring(L, 1, &param_len);
  TSUrlHttpQuerySet(http_ctx->server_request_bufp, http_ctx->server_request_url, param, param_len);

  return 0;
}

static int
ts_lua_server_request_get_uri_args(lua_State * L)
{
  const char *param;
  int param_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_SERVER_REQUEST_URL(http_ctx);

  param = TSUrlHttpQueryGet(http_ctx->server_request_bufp, http_ctx->server_request_url, &param_len);

  if (param && param_len > 0) {
    lua_pushlstring(L, param, param_len);

  } else {
    lua_pushnil(L);
  }

  return 1;
}
