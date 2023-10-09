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

#include "ts_lua_hook.h"
#include "ts_lua_transform.h"
#include "ts_lua_util.h"

typedef enum {
  TS_LUA_HOOK_DUMMY = 0,
  TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE,
  TS_LUA_HOOK_SEND_REQUEST_HDR,
  TS_LUA_HOOK_READ_RESPONSE_HDR,
  TS_LUA_HOOK_SEND_RESPONSE_HDR,
  TS_LUA_HOOK_READ_REQUEST_HDR,
  TS_LUA_HOOK_TXN_START,
  TS_LUA_HOOK_PRE_REMAP,
  TS_LUA_HOOK_POST_REMAP,
  TS_LUA_HOOK_OS_DNS,
  TS_LUA_HOOK_READ_CACHE_HDR,
  TS_LUA_HOOK_TXN_CLOSE,
  TS_LUA_REQUEST_TRANSFORM,
  TS_LUA_RESPONSE_TRANSFORM,
  TS_LUA_HOOK_VCONN_START,
  TS_LUA_HOOK_LAST
} TSLuaHookID;

char *ts_lua_hook_id_string[] = {"TS_LUA_HOOK_DUMMY",
                                 "TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE",
                                 "TS_LUA_HOOK_SEND_REQUEST_HDR",
                                 "TS_LUA_HOOK_READ_RESPONSE_HDR",
                                 "TS_LUA_HOOK_SEND_RESPONSE_HDR",
                                 "TS_LUA_HOOK_READ_REQUEST_HDR",
                                 "TS_LUA_HOOK_TXN_START",
                                 "TS_LUA_HOOK_PRE_REMAP",
                                 "TS_LUA_HOOK_POST_REMAP",
                                 "TS_LUA_HOOK_OS_DNS",
                                 "TS_LUA_HOOK_READ_CACHE_HDR",
                                 "TS_LUA_HOOK_TXN_CLOSE",
                                 "TS_LUA_REQUEST_TRANSFORM",
                                 "TS_LUA_RESPONSE_TRANSFORM",
                                 "TS_LUA_HOOK_VCONN_START",
                                 "TS_LUA_HOOK_LAST"};

static int ts_lua_add_hook(lua_State *L);
static void ts_lua_inject_hook_variables(lua_State *L);

void
ts_lua_inject_hook_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_add_hook);
  lua_setfield(L, -2, "hook");

  ts_lua_inject_hook_variables(L);
}

static void
ts_lua_inject_hook_variables(lua_State *L)
{
  size_t i;

  for (i = 0; i < sizeof(ts_lua_hook_id_string) / sizeof(char *); i++) {
    lua_pushinteger(L, (lua_Integer)i);
    lua_setglobal(L, ts_lua_hook_id_string[i]);
  }
}

static int
ts_lua_add_hook(lua_State *L)
{
  int type;
  int entry;

  TSVConn connp;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);

  entry = lua_tointeger(L, 1); // get hook id

  type = lua_type(L, 2);
  if (type != LUA_TFUNCTION)
    return 0;

  switch (entry) {
  case TS_LUA_HOOK_CACHE_LOOKUP_COMPLETE:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_CACHE_LOOKUP_COMPLETE);
    }
    break;

  case TS_LUA_HOOK_SEND_REQUEST_HDR:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_SEND_REQUEST);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_SEND_REQUEST);
    }
    break;

  case TS_LUA_HOOK_READ_RESPONSE_HDR:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_READ_RESPONSE);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_READ_RESPONSE);
    }
    break;

  case TS_LUA_HOOK_SEND_RESPONSE_HDR:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_SEND_RESPONSE);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_SEND_RESPONSE);
    }
    break;

  case TS_LUA_HOOK_READ_REQUEST_HDR:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_READ_REQUEST);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_READ_REQUEST);
    }
    break;

  case TS_LUA_HOOK_TXN_START:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_TXN_START_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_TXN_START);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_TXN_START);
    }
    break;

  case TS_LUA_HOOK_PRE_REMAP:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_PRE_REMAP_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_PRE_REMAP);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_PRE_REMAP);
    }
    break;

  case TS_LUA_HOOK_POST_REMAP:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_POST_REMAP_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_POST_REMAP);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_POST_REMAP);
    }
    break;

  case TS_LUA_HOOK_OS_DNS:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_OS_DNS_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_OS_DNS);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_OS_DNS);
    }
    break;

  case TS_LUA_HOOK_READ_CACHE_HDR:
    if (http_ctx) {
      TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_READ_CACHE_HDR_HOOK, http_ctx->cinfo.contp);
      http_ctx->has_hook = 1;
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_READ_CACHE);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_READ_CACHE);
    }
    break;

  case TS_LUA_HOOK_TXN_CLOSE:
    if (http_ctx) {
      // we don't need to add a hook because we already have added one by default
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_TXN_CLOSE);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_TXN_CLOSE);
    }
    break;

  case TS_LUA_REQUEST_TRANSFORM:
  case TS_LUA_RESPONSE_TRANSFORM:
    if (http_ctx) {
      connp = TSTransformCreate(ts_lua_transform_entry, http_ctx->txnp);
      ts_lua_create_http_transform_ctx(http_ctx, connp);

      if (entry == TS_LUA_REQUEST_TRANSFORM) {
        TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, connp);
      } else {
        TSHttpTxnHookAdd(http_ctx->txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
      }
    }
    break;

  case TS_LUA_HOOK_VCONN_START:
    if (http_ctx) {
      TSError("[ts_lua][%s] VCONN_START handler can only be global", __FUNCTION__);
    } else {
      lua_pushvalue(L, 2);
      lua_setglobal(L, TS_LUA_FUNCTION_G_VCONN_START);
    }

  default:
    break;
  }

  return 0;
}
