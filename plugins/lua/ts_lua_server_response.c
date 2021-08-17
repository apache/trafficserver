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

#define TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx)                                                                    \
  do {                                                                                                                \
    if (!http_ctx->server_response_hdrp) {                                                                            \
      if (TSHttpTxnServerRespGet(http_ctx->txnp, &http_ctx->server_response_bufp, &http_ctx->server_response_hdrp) != \
          TS_SUCCESS) {                                                                                               \
        return 0;                                                                                                     \
      }                                                                                                               \
    }                                                                                                                 \
  } while (0)

static void ts_lua_inject_server_response_header_api(lua_State *L);
static void ts_lua_inject_server_response_header_table_api(lua_State *L);
static void ts_lua_inject_server_response_headers_api(lua_State *L);
static void ts_lua_inject_server_response_misc_api(lua_State *L);

static int ts_lua_server_response_header_get(lua_State *L);
static int ts_lua_server_response_header_set(lua_State *L);

static int ts_lua_server_response_header_table_get(lua_State *L);
static int ts_lua_server_response_header_table_set(lua_State *L);

static int ts_lua_server_response_get_headers(lua_State *L);

static int ts_lua_server_response_get_status(lua_State *L);
static int ts_lua_server_response_set_status(lua_State *L);

static int ts_lua_server_response_get_version(lua_State *L);
static int ts_lua_server_response_set_version(lua_State *L);

static int ts_lua_server_response_is_cacheable(lua_State *L);
static int ts_lua_server_response_get_maxage(lua_State *L);

void
ts_lua_inject_server_response_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_server_response_header_api(L);
  ts_lua_inject_server_response_header_table_api(L);
  ts_lua_inject_server_response_headers_api(L);
  ts_lua_inject_server_response_misc_api(L);

  lua_setfield(L, -2, "server_response");
}

static void
ts_lua_inject_server_response_header_api(lua_State *L)
{
  lua_newtable(L); /* .header */

  lua_createtable(L, 0, 2); /* metatable for .header */

  lua_pushcfunction(L, ts_lua_server_response_header_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_server_response_header_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "header");
}

static void
ts_lua_inject_server_response_header_table_api(lua_State *L)
{
  lua_newtable(L); /* .header */

  lua_createtable(L, 0, 2); /* metatable for .header */

  lua_pushcfunction(L, ts_lua_server_response_header_table_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_server_response_header_table_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "header_table");
}

static void
ts_lua_inject_server_response_headers_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_server_response_get_headers);
  lua_setfield(L, -2, "get_headers");
}

static int
ts_lua_server_response_get_headers(lua_State *L)
{
  const char *name;
  const char *value;
  int name_len;
  int value_len;
  TSMLoc field_loc;
  TSMLoc next_field_loc;
  const char *tvalue;
  size_t tvalue_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  lua_newtable(L);

  field_loc = TSMimeHdrFieldGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, 0);

  while (field_loc != TS_NULL_MLOC) {
    name = TSMimeHdrFieldNameGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, &name_len);
    if (name && name_len) {
      // retrieve the header name from table
      lua_pushlstring(L, name, name_len);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        // if header name does not exist in the table, insert it
        lua_pop(L, 1);
        value =
          TSMimeHdrFieldValueStringGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, &value_len);
        lua_pushlstring(L, name, name_len);
        lua_pushlstring(L, value, value_len);
        lua_rawset(L, -3);
      } else {
        // if header name exists in the table, append a command and the new value to the end of the existing value
        tvalue = lua_tolstring(L, -1, &tvalue_len);
        lua_pop(L, 1);
        value =
          TSMimeHdrFieldValueStringGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, &value_len);
        lua_pushlstring(L, name, name_len);
        lua_pushlstring(L, tvalue, tvalue_len);
        lua_pushlstring(L, ",", 1);
        lua_pushlstring(L, value, value_len);
        lua_concat(L, 3);
        lua_rawset(L, -3);
      }
    }

    next_field_loc = TSMimeHdrFieldNext(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
    TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
    field_loc = next_field_loc;
  }

  return 1;
}

static void
ts_lua_inject_server_response_misc_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_server_response_get_status);
  lua_setfield(L, -2, "get_status");
  lua_pushcfunction(L, ts_lua_server_response_set_status);
  lua_setfield(L, -2, "set_status");

  lua_pushcfunction(L, ts_lua_server_response_get_version);
  lua_setfield(L, -2, "get_version");
  lua_pushcfunction(L, ts_lua_server_response_set_version);
  lua_setfield(L, -2, "set_version");

  lua_pushcfunction(L, ts_lua_server_response_is_cacheable);
  lua_setfield(L, -2, "is_cacheable");
  lua_pushcfunction(L, ts_lua_server_response_get_maxage);
  lua_setfield(L, -2, "get_maxage");
}

static int
ts_lua_server_response_header_get(lua_State *L)
{
  const char *key;
  const char *val;
  int val_len;
  size_t key_len;
  int count;

  TSMLoc field_loc, next_field_loc;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  /*  we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  if (key && key_len) {
    field_loc = TSMimeHdrFieldFind(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, key, key_len);
    if (field_loc != TS_NULL_MLOC) {
      count = 0;
      while (field_loc != TS_NULL_MLOC) {
        val = TSMimeHdrFieldValueStringGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, &val_len);
        next_field_loc = TSMimeHdrFieldNextDup(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
        lua_pushlstring(L, val, val_len);
        count++;
        // multiple headers with the same name must be semantically the same as one value which is comma separated
        if (next_field_loc != TS_NULL_MLOC) {
          lua_pushlstring(L, ",", 1);
          count++;
        }
        TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
        field_loc = next_field_loc;
      }
      lua_concat(L, count);
    } else {
      lua_pushnil(L);
    }

  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_server_response_header_set(lua_State *L)
{
  const char *key;
  const char *val;
  size_t val_len;
  size_t key_len;
  int remove;
  int first;

  TSMLoc field_loc, tmp;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  remove = 0;
  val    = NULL;

  /*   we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);
  if (lua_isnil(L, 3)) {
    remove = 1;
  } else {
    val = luaL_checklstring(L, 3, &val_len);
  }

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  field_loc = TSMimeHdrFieldFind(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, key, key_len);

  if (remove) {
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      TSMimeHdrFieldDestroy(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      field_loc = tmp;
    }
  } else if (field_loc != TS_NULL_MLOC) {
    first = 1;
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      if (first) {
        first = 0;
        TSMimeHdrFieldValueStringSet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, val, val_len);
      } else {
        TSMimeHdrFieldDestroy(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      }
      TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, key, key_len, &field_loc) !=
             TS_SUCCESS) {
    TSError("[ts_lua][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
    return 0;

  } else {
    TSMimeHdrFieldValueStringSet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, val, val_len);
    TSMimeHdrFieldAppend(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
  }

  if (field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
  }

  return 0;
}

static int
ts_lua_server_response_header_table_get(lua_State *L)
{
  const char *key;
  const char *val;
  int val_len;
  size_t key_len;
  int count;

  TSMLoc field_loc, next_field_loc;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  /*  we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  if (key && key_len) {
    field_loc = TSMimeHdrFieldFind(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, key, key_len);

    if (field_loc != TS_NULL_MLOC) {
      lua_newtable(L);
      count = 0;
      while (field_loc != TS_NULL_MLOC) {
        val = TSMimeHdrFieldValueStringGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc, -1, &val_len);
        next_field_loc = TSMimeHdrFieldNextDup(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
        count++;

        lua_pushlstring(L, val, val_len);
        lua_rawseti(L, -2, count);

        TSHandleMLocRelease(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, field_loc);
        field_loc = next_field_loc;
      }

    } else {
      lua_pushnil(L);
    }

  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_server_response_header_table_set(lua_State *L)
{
  return 0;
}

static int
ts_lua_server_response_get_status(lua_State *L)
{
  int status;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  status = TSHttpHdrStatusGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp);

  lua_pushinteger(L, status);

  return 1;
}

static int
ts_lua_server_response_set_status(lua_State *L)
{
  int status;
  const char *reason;
  int reason_len = 0;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  // NOLINTNEXTLINE
  status = luaL_checkint(L, 1);

  reason = TSHttpHdrReasonLookup(status);
  if (reason) {
    reason_len = strlen(reason);
  }

  TSHttpHdrStatusSet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, status);
  TSHttpHdrReasonSet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, reason, reason_len);

  return 0;
}

static int
ts_lua_server_response_get_version(lua_State *L)
{
  int version;
  char buf[32];
  int n;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  version = TSHttpHdrVersionGet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp);

  n = snprintf(buf, sizeof(buf), "%d.%d", TS_HTTP_MAJOR(version), TS_HTTP_MINOR(version));

  if (n >= (int)sizeof(buf)) {
    lua_pushlstring(L, buf, sizeof(buf) - 1);
  } else if (n > 0) {
    lua_pushlstring(L, buf, n);
  }

  return 1;
}

static int
ts_lua_server_response_set_version(lua_State *L)
{
  const char *version;
  size_t len;
  unsigned int major, minor;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  version = luaL_checklstring(L, 1, &len);

  if (sscanf(version, "%2u.%2u", &major, &minor) != 2) {
    return luaL_error(L, "failed to set version. Format must be X.Y");
  }

  TSHttpHdrVersionSet(http_ctx->server_response_bufp, http_ctx->server_response_hdrp, TS_HTTP_VERSION(major, minor));

  return 0;
}

static int
ts_lua_server_response_is_cacheable(lua_State *L)
{
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  lua_pushnumber(L, TSHttpTxnIsCacheable(http_ctx->txnp, NULL, http_ctx->server_response_bufp));

  return 1;
}

static int
ts_lua_server_response_get_maxage(lua_State *L)
{
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  TS_LUA_CHECK_SERVER_RESPONSE_HDR(http_ctx);

  lua_pushnumber(L, TSHttpTxnGetMaxAge(http_ctx->txnp, http_ctx->server_response_bufp));

  return 1;
}
