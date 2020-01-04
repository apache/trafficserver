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

#include <netinet/in.h>
#include <arpa/inet.h>
#include "ts_lua_util.h"

static void ts_lua_inject_client_request_client_addr_api(lua_State *L);
static void ts_lua_inject_client_request_server_addr_api(lua_State *L);

static int ts_lua_client_request_header_get(lua_State *L);
static int ts_lua_client_request_header_set(lua_State *L);
static int ts_lua_client_request_get_headers(lua_State *L);
static int ts_lua_client_request_get_url(lua_State *L);
static int ts_lua_client_request_get_pristine_url(lua_State *L);
static int ts_lua_client_request_get_url_host(lua_State *L);
static int ts_lua_client_request_set_url_host(lua_State *L);
static int ts_lua_client_request_get_url_port(lua_State *L);
static int ts_lua_client_request_set_url_port(lua_State *L);
static int ts_lua_client_request_get_url_scheme(lua_State *L);
static int ts_lua_client_request_set_url_scheme(lua_State *L);

static int ts_lua_client_request_get_uri(lua_State *L);
static int ts_lua_client_request_set_uri(lua_State *L);
static int ts_lua_client_request_set_uri_args(lua_State *L);
static int ts_lua_client_request_get_uri_args(lua_State *L);
static int ts_lua_client_request_set_uri_params(lua_State *L);
static int ts_lua_client_request_get_uri_params(lua_State *L);
static int ts_lua_client_request_get_method(lua_State *L);
static int ts_lua_client_request_set_method(lua_State *L);
static int ts_lua_client_request_get_version(lua_State *L);
static int ts_lua_client_request_set_version(lua_State *L);

static int ts_lua_client_request_get_body_size(lua_State *L);
static int ts_lua_client_request_get_header_size(lua_State *L);

static void ts_lua_inject_client_request_socket_api(lua_State *L);
static void ts_lua_inject_client_request_header_api(lua_State *L);
static void ts_lua_inject_client_request_headers_api(lua_State *L);
static void ts_lua_inject_client_request_url_api(lua_State *L);
static void ts_lua_inject_client_request_uri_api(lua_State *L);
static void ts_lua_inject_client_request_args_api(lua_State *L);
static void ts_lua_inject_client_request_params_api(lua_State *L);
static void ts_lua_inject_client_request_method_api(lua_State *L);
static void ts_lua_inject_client_request_version_api(lua_State *L);
static void ts_lua_inject_client_request_body_size_api(lua_State *L);
static void ts_lua_inject_client_request_header_size_api(lua_State *L);

static int ts_lua_client_request_client_addr_get_ip(lua_State *L);
static int ts_lua_client_request_client_addr_get_port(lua_State *L);
static int ts_lua_client_request_client_addr_get_addr(lua_State *L);
static int ts_lua_client_request_client_addr_get_incoming_port(lua_State *L);

static void ts_lua_inject_client_request_ssl_reused_api(lua_State *L);
static int ts_lua_client_request_get_ssl_reused(lua_State *L);
static void ts_lua_inject_client_request_ssl_cipher_api(lua_State *L);
static int ts_lua_client_request_get_ssl_cipher(lua_State *L);
static void ts_lua_inject_client_request_ssl_protocol_api(lua_State *L);
static int ts_lua_client_request_get_ssl_protocol(lua_State *L);
static void ts_lua_inject_client_request_ssl_curve_api(lua_State *L);
static int ts_lua_client_request_get_ssl_curve(lua_State *L);

void
ts_lua_inject_client_request_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_client_request_socket_api(L);
  ts_lua_inject_client_request_header_api(L);
  ts_lua_inject_client_request_headers_api(L);
  ts_lua_inject_client_request_url_api(L);
  ts_lua_inject_client_request_uri_api(L);
  ts_lua_inject_client_request_args_api(L);
  ts_lua_inject_client_request_params_api(L);
  ts_lua_inject_client_request_method_api(L);
  ts_lua_inject_client_request_version_api(L);
  ts_lua_inject_client_request_body_size_api(L);
  ts_lua_inject_client_request_header_size_api(L);
  ts_lua_inject_client_request_ssl_reused_api(L);
  ts_lua_inject_client_request_ssl_cipher_api(L);
  ts_lua_inject_client_request_ssl_protocol_api(L);
  ts_lua_inject_client_request_ssl_curve_api(L);

  lua_setfield(L, -2, "client_request");
}

static void
ts_lua_inject_client_request_socket_api(lua_State *L)
{
  ts_lua_inject_client_request_client_addr_api(L);
  ts_lua_inject_client_request_server_addr_api(L);
}

static void
ts_lua_inject_client_request_client_addr_api(lua_State *L)
{
  lua_newtable(L);

  lua_pushcfunction(L, ts_lua_client_request_client_addr_get_ip);
  lua_setfield(L, -2, "get_ip");

  lua_pushcfunction(L, ts_lua_client_request_client_addr_get_port);
  lua_setfield(L, -2, "get_port");

  lua_pushcfunction(L, ts_lua_client_request_client_addr_get_addr);
  lua_setfield(L, -2, "get_addr");

  lua_pushcfunction(L, ts_lua_client_request_client_addr_get_incoming_port);
  lua_setfield(L, -2, "get_incoming_port");

  lua_setfield(L, -2, "client_addr");
}

static void
ts_lua_inject_client_request_server_addr_api(lua_State *L ATS_UNUSED)
{
  return;
}

static void
ts_lua_inject_client_request_header_api(lua_State *L)
{
  lua_newtable(L); /* .header */

  lua_createtable(L, 0, 2); /* metatable for .header */

  lua_pushcfunction(L, ts_lua_client_request_header_get);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, ts_lua_client_request_header_set);
  lua_setfield(L, -2, "__newindex");

  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "header");
}

static int
ts_lua_client_request_header_get(lua_State *L)
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

  if (key && key_len) {
    field_loc = TSMimeHdrFieldFind(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, key, key_len);

    if (field_loc != TS_NULL_MLOC) {
      count = 0;
      while (field_loc != TS_NULL_MLOC) {
        val = TSMimeHdrFieldValueStringGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, &val_len);
        next_field_loc = TSMimeHdrFieldNextDup(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
        lua_pushlstring(L, val, val_len);
        count++;
        // multiple headers with the same name must be semantically the same as one value which is comma separated
        if (next_field_loc != TS_NULL_MLOC) {
          lua_pushlstring(L, ",", 1);
          count++;
        }
        TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
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
ts_lua_client_request_header_set(lua_State *L)
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

  field_loc = TSMimeHdrFieldFind(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, key, key_len);

  if (remove) {
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      TSMimeHdrFieldDestroy(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      field_loc = tmp;
    }
  } else if (field_loc != TS_NULL_MLOC) {
    first = 1;
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      if (first) {
        first = 0;
        TSMimeHdrFieldValueStringSet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, val, val_len);
      } else {
        TSMimeHdrFieldDestroy(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      }
      TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, key, key_len, &field_loc) !=
             TS_SUCCESS) {
    TSError("[ts_lua][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
    return 0;

  } else {
    TSMimeHdrFieldValueStringSet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, val, val_len);
    TSMimeHdrFieldAppend(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
  }

  if (field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
  }

  return 0;
}

static void
ts_lua_inject_client_request_headers_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_headers);
  lua_setfield(L, -2, "get_headers");
}

static int
ts_lua_client_request_get_headers(lua_State *L)
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

  lua_newtable(L);

  field_loc = TSMimeHdrFieldGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, 0);

  while (field_loc != TS_NULL_MLOC) {
    name = TSMimeHdrFieldNameGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, &name_len);
    if (name && name_len) {
      // retrieve the header name from table
      lua_pushlstring(L, name, name_len);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        // if header name does not exist in the table, insert it
        lua_pop(L, 1);
        value =
          TSMimeHdrFieldValueStringGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, &value_len);
        lua_pushlstring(L, name, name_len);
        lua_pushlstring(L, value, value_len);
        lua_rawset(L, -3);
      } else {
        // if header name exists in the table, append a command and the new value to the end of the existing value
        tvalue = lua_tolstring(L, -1, &tvalue_len);
        lua_pop(L, 1);
        value =
          TSMimeHdrFieldValueStringGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, &value_len);
        lua_pushlstring(L, name, name_len);
        lua_pushlstring(L, tvalue, tvalue_len);
        lua_pushlstring(L, ",", 1);
        lua_pushlstring(L, value, value_len);
        lua_concat(L, 3);
        lua_rawset(L, -3);
      }
    }

    next_field_loc = TSMimeHdrFieldNext(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
    TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
    field_loc = next_field_loc;
  }

  return 1;
}

static void
ts_lua_inject_client_request_url_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_url);
  lua_setfield(L, -2, "get_url");

  lua_pushcfunction(L, ts_lua_client_request_get_pristine_url);
  lua_setfield(L, -2, "get_pristine_url");

  lua_pushcfunction(L, ts_lua_client_request_get_url_host);
  lua_setfield(L, -2, "get_url_host");
  lua_pushcfunction(L, ts_lua_client_request_set_url_host);
  lua_setfield(L, -2, "set_url_host");

  lua_pushcfunction(L, ts_lua_client_request_get_url_port);
  lua_setfield(L, -2, "get_url_port");
  lua_pushcfunction(L, ts_lua_client_request_set_url_port);
  lua_setfield(L, -2, "set_url_port");

  lua_pushcfunction(L, ts_lua_client_request_get_url_scheme);
  lua_setfield(L, -2, "get_url_scheme");
  lua_pushcfunction(L, ts_lua_client_request_set_url_scheme);
  lua_setfield(L, -2, "set_url_scheme");
}

static void
ts_lua_inject_client_request_uri_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_set_uri);
  lua_setfield(L, -2, "set_uri");

  lua_pushcfunction(L, ts_lua_client_request_get_uri);
  lua_setfield(L, -2, "get_uri");
}

static int
ts_lua_client_request_get_url(lua_State *L)
{
  char *url;
  int url_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  url = TSHttpTxnEffectiveUrlStringGet(http_ctx->txnp, &url_len);

  if (url) {
    lua_pushlstring(L, url, url_len);
    TSfree(url);

  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_client_request_get_pristine_url(lua_State *L)
{
  char *url;
  int url_len;

  TSMBuffer bufp;
  TSMLoc url_loc;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  if (TSHttpTxnPristineUrlGet(http_ctx->txnp, &bufp, &url_loc) != TS_SUCCESS) {
    return 0;
  }

  url = TSUrlStringGet(bufp, url_loc, &url_len);

  if (url) {
    lua_pushlstring(L, url, url_len);
    TSfree(url);

  } else {
    lua_pushnil(L);
  }

  TSHandleMLocRelease(bufp, NULL, url_loc);

  return 1;
}

static int
ts_lua_client_request_get_url_host(lua_State *L)
{
  const char *host;
  int len = 0;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  host = TSUrlHostGet(http_ctx->client_request_bufp, http_ctx->client_request_url, &len);

  if (len == 0) {
    char *key   = "Host";
    char *l_key = "host";
    int key_len = 4;

    TSMLoc field_loc;

    field_loc = TSMimeHdrFieldFind(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, key, key_len);
    if (field_loc) {
      host = TSMimeHdrFieldValueStringGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, &len);
      TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);

    } else {
      field_loc = TSMimeHdrFieldFind(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, l_key, key_len);
      if (field_loc) {
        host = TSMimeHdrFieldValueStringGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc, -1, &len);
        TSHandleMLocRelease(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, field_loc);
      }
    }
  }

  lua_pushlstring(L, host, len);

  return 1;
}

static int
ts_lua_client_request_set_url_host(lua_State *L)
{
  const char *host;
  size_t len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  host = luaL_checklstring(L, 1, &len);

  TSUrlHostSet(http_ctx->client_request_bufp, http_ctx->client_request_url, host, len);

  return 0;
}

static int
ts_lua_client_request_get_url_port(lua_State *L)
{
  int port;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  port = TSUrlPortGet(http_ctx->client_request_bufp, http_ctx->client_request_url);

  lua_pushnumber(L, port);

  return 1;
}

static int
ts_lua_client_request_set_url_port(lua_State *L)
{
  int port;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  // NOLINTNEXTLINE
  port = luaL_checkint(L, 1);

  TSUrlPortSet(http_ctx->client_request_bufp, http_ctx->client_request_url, port);

  return 0;
}

static int
ts_lua_client_request_get_url_scheme(lua_State *L)
{
  const char *scheme;
  int len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  scheme = TSUrlSchemeGet(http_ctx->client_request_bufp, http_ctx->client_request_url, &len);

  lua_pushlstring(L, scheme, len);

  return 1;
}

static int
ts_lua_client_request_set_url_scheme(lua_State *L)
{
  const char *scheme;
  size_t len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  scheme = luaL_checklstring(L, 1, &len);

  TSUrlSchemeSet(http_ctx->client_request_bufp, http_ctx->client_request_url, scheme, len);

  return 0;
}

static int
ts_lua_client_request_get_uri(lua_State *L)
{
  const char *path;
  int path_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  path = TSUrlPathGet(http_ctx->client_request_bufp, http_ctx->client_request_url, &path_len);

  lua_pushlstring(L, "/", 1);
  lua_pushlstring(L, path, path_len >= TS_LUA_MAX_URL_LENGTH - 1 ? TS_LUA_MAX_URL_LENGTH - 2 : path_len);
  lua_concat(L, 2);

  return 1;
}

static int
ts_lua_client_request_set_uri(lua_State *L)
{
  const char *path;
  size_t path_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  path = luaL_checklstring(L, 1, &path_len);

  if (*path == '/') {
    path++;
    path_len--;
  }

  TSUrlPathSet(http_ctx->client_request_bufp, http_ctx->client_request_url, path, path_len);

  return 0;
}

static void
ts_lua_inject_client_request_args_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_set_uri_args);
  lua_setfield(L, -2, "set_uri_args");

  lua_pushcfunction(L, ts_lua_client_request_get_uri_args);
  lua_setfield(L, -2, "get_uri_args");
}

static int
ts_lua_client_request_get_uri_args(lua_State *L)
{
  const char *param;
  int param_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  param = TSUrlHttpQueryGet(http_ctx->client_request_bufp, http_ctx->client_request_url, &param_len);

  if (param && param_len > 0) {
    lua_pushlstring(L, param, param_len);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_client_request_set_uri_args(lua_State *L)
{
  const char *param;
  size_t param_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  param = luaL_checklstring(L, 1, &param_len);
  TSUrlHttpQuerySet(http_ctx->client_request_bufp, http_ctx->client_request_url, param, param_len);

  return 0;
}

static void
ts_lua_inject_client_request_params_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_set_uri_params);
  lua_setfield(L, -2, "set_uri_params");

  lua_pushcfunction(L, ts_lua_client_request_get_uri_params);
  lua_setfield(L, -2, "get_uri_params");
}

static int
ts_lua_client_request_get_uri_params(lua_State *L)
{
  const char *param;
  int param_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  param = TSUrlHttpParamsGet(http_ctx->client_request_bufp, http_ctx->client_request_url, &param_len);

  if (param && param_len > 0) {
    lua_pushlstring(L, param, param_len);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_client_request_set_uri_params(lua_State *L)
{
  const char *param;
  size_t param_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  param = luaL_checklstring(L, 1, &param_len);
  TSUrlHttpParamsSet(http_ctx->client_request_bufp, http_ctx->client_request_url, param, param_len);

  return 0;
}

static int
ts_lua_client_request_client_addr_get_ip(lua_State *L)
{
  struct sockaddr const *client_ip;
  char cip[128];
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  client_ip = TSHttpTxnClientAddrGet(http_ctx->txnp);

  if (client_ip == NULL) {
    lua_pushnil(L);

  } else {
    if (client_ip->sa_family == AF_INET) {
      inet_ntop(AF_INET, (const void *)&((struct sockaddr_in *)client_ip)->sin_addr, cip, sizeof(cip));
    } else {
      inet_ntop(AF_INET6, (const void *)&((struct sockaddr_in6 *)client_ip)->sin6_addr, cip, sizeof(cip));
    }

    lua_pushstring(L, cip);
  }

  return 1;
}

static int
ts_lua_client_request_client_addr_get_port(lua_State *L)
{
  struct sockaddr const *client_ip;
  ts_lua_http_ctx *http_ctx;
  int port;

  GET_HTTP_CONTEXT(http_ctx, L);

  client_ip = TSHttpTxnClientAddrGet(http_ctx->txnp);

  if (client_ip == NULL) {
    lua_pushnil(L);

  } else {
    if (client_ip->sa_family == AF_INET) {
      port = ((struct sockaddr_in *)client_ip)->sin_port;
    } else {
      port = ((struct sockaddr_in6 *)client_ip)->sin6_port;
    }

    lua_pushnumber(L, port);
  }

  return 1;
}

static int
ts_lua_client_request_client_addr_get_incoming_port(lua_State *L)
{
  struct sockaddr const *incoming_addr;
  ts_lua_http_ctx *http_ctx;
  int port;

  GET_HTTP_CONTEXT(http_ctx, L);

  incoming_addr = TSHttpTxnIncomingAddrGet(http_ctx->txnp);

  if (incoming_addr == NULL) {
    lua_pushnil(L);

  } else {
    if (incoming_addr->sa_family == AF_INET) {
      port = ((struct sockaddr_in *)incoming_addr)->sin_port;
    } else {
      port = ((struct sockaddr_in6 *)incoming_addr)->sin6_port;
    }

    lua_pushnumber(L, ntohs(port));
  }

  return 1;
}

static int
ts_lua_client_request_client_addr_get_addr(lua_State *L)
{
  struct sockaddr const *client_ip;
  ts_lua_http_ctx *http_ctx;
  int port;
  int family;
  char cip[128];

  GET_HTTP_CONTEXT(http_ctx, L);

  client_ip = TSHttpTxnClientAddrGet(http_ctx->txnp);

  if (client_ip == NULL) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);

  } else {
    if (client_ip->sa_family == AF_INET) {
      port = ntohs(((struct sockaddr_in *)client_ip)->sin_port);
      inet_ntop(AF_INET, (const void *)&((struct sockaddr_in *)client_ip)->sin_addr, cip, sizeof(cip));
      family = AF_INET;
    } else {
      port = ntohs(((struct sockaddr_in6 *)client_ip)->sin6_port);
      inet_ntop(AF_INET6, (const void *)&((struct sockaddr_in6 *)client_ip)->sin6_addr, cip, sizeof(cip));
      family = AF_INET6;
    }

    lua_pushstring(L, cip);
    lua_pushnumber(L, port);
    lua_pushnumber(L, family);
  }

  return 3;
}

static void
ts_lua_inject_client_request_method_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_method);
  lua_setfield(L, -2, "get_method");

  lua_pushcfunction(L, ts_lua_client_request_set_method);
  lua_setfield(L, -2, "set_method");
}

static int
ts_lua_client_request_get_method(lua_State *L)
{
  const char *method;
  int method_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  method = TSHttpHdrMethodGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, &method_len);

  if (method && method_len) {
    lua_pushlstring(L, method, method_len);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static int
ts_lua_client_request_set_method(lua_State *L)
{
  const char *method;
  size_t method_len;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  method = luaL_checklstring(L, 1, &method_len);

  if (method) {
    TSHttpHdrMethodSet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, method, method_len);
  }

  return 0;
}

static void
ts_lua_inject_client_request_body_size_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_body_size);
  lua_setfield(L, -2, "get_body_size");
}

static int
ts_lua_client_request_get_body_size(lua_State *L)
{
  int64_t body_size;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  body_size = TSHttpTxnClientReqBodyBytesGet(http_ctx->txnp);
  lua_pushnumber(L, body_size);

  return 1;
}

static void
ts_lua_inject_client_request_version_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_version);
  lua_setfield(L, -2, "get_version");

  lua_pushcfunction(L, ts_lua_client_request_set_version);
  lua_setfield(L, -2, "set_version");
}

static int
ts_lua_client_request_get_version(lua_State *L)
{
  int version;
  char buf[32];
  int n;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  version = TSHttpHdrVersionGet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp);

  n = snprintf(buf, sizeof(buf), "%d.%d", TS_HTTP_MAJOR(version), TS_HTTP_MINOR(version));
  if (n >= (int)sizeof(buf)) {
    lua_pushlstring(L, buf, sizeof(buf) - 1);
  } else if (n > 0) {
    lua_pushlstring(L, buf, n);
  }

  return 1;
}

static int
ts_lua_client_request_set_version(lua_State *L)
{
  const char *version;
  size_t len;
  unsigned int major, minor;

  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  version = luaL_checklstring(L, 1, &len);

  if (sscanf(version, "%2u.%2u", &major, &minor) != 2) {
    return luaL_error(L, "failed to set version. Format must be X.Y");
  }

  TSHttpHdrVersionSet(http_ctx->client_request_bufp, http_ctx->client_request_hdrp, TS_HTTP_VERSION(major, minor));

  return 0;
}

static void
ts_lua_inject_client_request_header_size_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_header_size);
  lua_setfield(L, -2, "get_header_size");
}

static int
ts_lua_client_request_get_header_size(lua_State *L)
{
  int header_size;
  ts_lua_http_ctx *http_ctx;

  GET_HTTP_CONTEXT(http_ctx, L);

  header_size = TSHttpTxnClientReqHdrBytesGet(http_ctx->txnp);
  lua_pushnumber(L, header_size);

  return 1;
}

static void
ts_lua_inject_client_request_ssl_reused_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_ssl_reused);
  lua_setfield(L, -2, "get_ssl_reused");
}

static int
ts_lua_client_request_get_ssl_reused(lua_State *L)
{
  int ssl_reused = 0;
  ts_lua_http_ctx *http_ctx;
  TSHttpSsn ssnp;
  TSVConn client_conn;

  GET_HTTP_CONTEXT(http_ctx, L);
  ssnp        = TSHttpTxnSsnGet(http_ctx->txnp);
  client_conn = TSHttpSsnClientVConnGet(ssnp);

  if (TSVConnIsSsl(client_conn)) {
    ssl_reused = TSVConnIsSslReused(client_conn);
  }

  lua_pushnumber(L, ssl_reused);

  return 1;
}

static void
ts_lua_inject_client_request_ssl_cipher_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_ssl_cipher);
  lua_setfield(L, -2, "get_ssl_cipher");
}

static int
ts_lua_client_request_get_ssl_cipher(lua_State *L)
{
  const char *ssl_cipher = "-";
  ts_lua_http_ctx *http_ctx;
  TSHttpSsn ssnp;
  TSVConn client_conn;

  GET_HTTP_CONTEXT(http_ctx, L);

  ssnp        = TSHttpTxnSsnGet(http_ctx->txnp);
  client_conn = TSHttpSsnClientVConnGet(ssnp);

  if (TSVConnIsSsl(client_conn)) {
    ssl_cipher = TSVConnSslCipherGet(client_conn);
  }

  lua_pushstring(L, ssl_cipher);

  return 1;
}

static void
ts_lua_inject_client_request_ssl_protocol_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_ssl_protocol);
  lua_setfield(L, -2, "get_ssl_protocol");
}

static int
ts_lua_client_request_get_ssl_protocol(lua_State *L)
{
  const char *ssl_protocol = "-";
  ts_lua_http_ctx *http_ctx;
  TSHttpSsn ssnp;
  TSVConn client_conn;

  GET_HTTP_CONTEXT(http_ctx, L);

  ssnp        = TSHttpTxnSsnGet(http_ctx->txnp);
  client_conn = TSHttpSsnClientVConnGet(ssnp);

  if (TSVConnIsSsl(client_conn)) {
    ssl_protocol = TSVConnSslProtocolGet(client_conn);
  }

  lua_pushstring(L, ssl_protocol);

  return 1;
}

static void
ts_lua_inject_client_request_ssl_curve_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_client_request_get_ssl_curve);
  lua_setfield(L, -2, "get_ssl_curve");
}

static int
ts_lua_client_request_get_ssl_curve(lua_State *L)
{
  const char *ssl_curve = "-";
  ts_lua_http_ctx *http_ctx;
  TSHttpSsn ssnp;
  TSVConn client_conn;

  GET_HTTP_CONTEXT(http_ctx, L);

  ssnp        = TSHttpTxnSsnGet(http_ctx->txnp);
  client_conn = TSHttpSsnClientVConnGet(ssnp);

  if (TSVConnIsSsl(client_conn)) {
    ssl_curve = TSVConnSslCurveGet(client_conn);
  }

  lua_pushstring(L, ssl_curve);

  return 1;
}
