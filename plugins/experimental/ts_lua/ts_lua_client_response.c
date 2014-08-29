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

#define TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx)     \
do {        \
    if (!http_ctx->client_response_hdrp) {           \
        if (TSHttpTxnClientRespGet(http_ctx->txnp,   \
                    &http_ctx->client_response_bufp, \
                    &http_ctx->client_response_hdrp) != TS_SUCCESS) {    \
            return 0;   \
        }   \
    }   \
} while(0)


static int ts_lua_client_response_header_get(lua_State * L);
static int ts_lua_client_response_header_set(lua_State * L);

static int ts_lua_client_response_get_headers(lua_State * L);

static int ts_lua_client_response_get_status(lua_State * L);
static int ts_lua_client_response_set_status(lua_State * L);

static int ts_lua_client_response_set_error_resp(lua_State * L);

static int ts_lua_client_response_get_version(lua_State * L);
static int ts_lua_client_response_set_version(lua_State * L);

static void ts_lua_inject_client_response_header_api(lua_State * L);
static void ts_lua_inject_client_response_headers_api(lua_State * L);
static void ts_lua_inject_client_response_misc_api(lua_State * L);


void
ts_lua_inject_client_response_api(lua_State * L)
{
  lua_newtable(L);

  ts_lua_inject_client_response_header_api(L);
  ts_lua_inject_client_response_headers_api(L);
  ts_lua_inject_client_response_misc_api(L);

  lua_setfield(L, -2, "client_response");
}

static void
ts_lua_inject_client_response_header_api(lua_State * L)
{
  lua_newtable(L);              /* .header */

  lua_createtable(L, 0, 2);     /* metatable for .header */

  lua_pushcfunction(L, ts_lua_client_response_header_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_client_response_header_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "header");
}

static int
ts_lua_client_response_header_get(lua_State * L)
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

  if (!http_ctx->client_response_hdrp) {
    if (TSHttpTxnClientRespGet(http_ctx->txnp,
                               &http_ctx->client_response_bufp, &http_ctx->client_response_hdrp) != TS_SUCCESS) {

      lua_pushnil(L);
      return 1;
    }
  }

  if (key && key_len) {

    field_loc = TSMimeHdrFieldFind(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, key, key_len);
    if (field_loc) {
      val =
        TSMimeHdrFieldValueStringGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc, -1,
                                     &val_len);
      lua_pushlstring(L, val, val_len);
      TSHandleMLocRelease(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);

    } else {
      lua_pushnil(L);
    }

  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_client_response_header_set(lua_State * L)
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

  /*  we skip the first argument that is the table */
  key = luaL_checklstring(L, 2, &key_len);
  if (lua_isnil(L, 3)) {
    remove = 1;
  } else {
    val = luaL_checklstring(L, 3, &val_len);
  }

  if (!http_ctx->client_response_hdrp) {
    if (TSHttpTxnClientRespGet(http_ctx->txnp, &http_ctx->client_response_bufp,
                               &http_ctx->client_response_hdrp) != TS_SUCCESS) {
      return 0;
    }
  }

  field_loc = TSMimeHdrFieldFind(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, key, key_len);

  if (remove) {
    if (field_loc) {
      TSMimeHdrFieldDestroy(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
    }

  } else if (field_loc) {
    TSMimeHdrFieldValueStringSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc, 0, val,
                                 val_len);

  } else if (TSMimeHdrFieldCreateNamed(http_ctx->client_response_bufp, http_ctx->client_response_hdrp,
                                       key, key_len, &field_loc) != TS_SUCCESS) {
    TSError("[%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
    return 0;

  } else {
    TSMimeHdrFieldValueStringSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc, -1, val,
                                 val_len);
    TSMimeHdrFieldAppend(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
  }

  if (field_loc)
    TSHandleMLocRelease(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);

  return 0;
}

static void
ts_lua_inject_client_response_headers_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_client_response_get_headers);
  lua_setfield(L, -2, "get_headers");
}

static int
ts_lua_client_response_get_headers(lua_State * L)
{
  const char *name;
  const char *value;
  int name_len;
  int value_len;
  TSMLoc field_loc;
  TSMLoc next_field_loc;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  lua_newtable(L);

  field_loc = TSMimeHdrFieldGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, 0);

  while (field_loc) {

    name = TSMimeHdrFieldNameGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc, &name_len);
    if (name && name_len) {

      value =
        TSMimeHdrFieldValueStringGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc, -1,
                                     &value_len);
      lua_pushlstring(L, name, name_len);
      lua_pushlstring(L, value, value_len);
      lua_rawset(L, -3);
    }

    next_field_loc = TSMimeHdrFieldNext(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
    TSHandleMLocRelease(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
    field_loc = next_field_loc;
  }

  return 1;
}

static void
ts_lua_inject_client_response_misc_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_client_response_get_status);
  lua_setfield(L, -2, "get_status");
  lua_pushcfunction(L, ts_lua_client_response_set_status);
  lua_setfield(L, -2, "set_status");

  lua_pushcfunction(L, ts_lua_client_response_get_version);
  lua_setfield(L, -2, "get_version");
  lua_pushcfunction(L, ts_lua_client_response_set_version);
  lua_setfield(L, -2, "set_version");

  lua_pushcfunction(L, ts_lua_client_response_set_error_resp);
  lua_setfield(L, -2, "set_error_resp");

  return;
}

static int
ts_lua_client_response_get_status(lua_State * L)
{
  int status;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  status = TSHttpHdrStatusGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp);

  lua_pushinteger(L, status);

  return 1;
}

static int
ts_lua_client_response_set_status(lua_State * L)
{
  int status;
  const char *reason;
  int reason_len;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  status = luaL_checkint(L, 1);

  reason = TSHttpHdrReasonLookup(status);
  reason_len = strlen(reason);

  TSHttpHdrStatusSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, status);
  TSHttpHdrReasonSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, reason, reason_len);

  return 0;
}

static int
ts_lua_client_response_get_version(lua_State * L)
{
  int version;
  char buf[32];
  int n;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  version = TSHttpHdrVersionGet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp);

  n = snprintf(buf, sizeof(buf) - 1, "%d.%d", TS_HTTP_MAJOR(version), TS_HTTP_MINOR(version));
  lua_pushlstring(L, buf, n);

  return 1;
}

static int
ts_lua_client_response_set_version(lua_State * L)
{
  const char *version;
  size_t len;
  int major, minor;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  version = luaL_checklstring(L, 1, &len);

  sscanf(version, "%2u.%2u", &major, &minor);

  TSHttpHdrVersionSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, TS_HTTP_VERSION(major, minor));

  return 0;
}

static int
ts_lua_client_response_set_error_resp(lua_State * L)
{
  int n, status;
  const char *body;
  const char *reason;
  int reason_len;
  size_t body_len;
  int resp_len;
  char *resp_buf;
  TSMLoc field_loc;

  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);
  TS_LUA_CHECK_CLIENT_RESPONSE_HDR(http_ctx);

  n = lua_gettop(L);

  status = luaL_checkinteger(L, 1);

  reason = TSHttpHdrReasonLookup(status);
  reason_len = strlen(reason);

  TSHttpHdrStatusSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, status);
  TSHttpHdrReasonSet(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, reason, reason_len);

  body_len = 0;

  if (n == 2) {
    body = luaL_checklstring(L, 2, &body_len);
  }

  if (body_len && body) {
    resp_buf = TSmalloc(body_len);
    memcpy(resp_buf, body, body_len);
    resp_len = body_len;

  } else {
    resp_buf = TSmalloc(reason_len);
    memcpy(resp_buf, reason, reason_len);
    resp_len = reason_len;
  }

  field_loc = TSMimeHdrFieldFind(http_ctx->client_response_bufp, http_ctx->client_response_hdrp,
                                 TS_MIME_FIELD_TRANSFER_ENCODING, TS_MIME_LEN_TRANSFER_ENCODING);

  if (field_loc) {
    TSMimeHdrFieldDestroy(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
    TSHandleMLocRelease(http_ctx->client_response_bufp, http_ctx->client_response_hdrp, field_loc);
  }

  TSHttpTxnErrorBodySet(http_ctx->txnp, resp_buf, resp_len, NULL);

  return 0;
}
