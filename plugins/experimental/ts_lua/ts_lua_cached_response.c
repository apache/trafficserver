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

static void ts_lua_inject_cached_response_header_misc_api(lua_State *L);
static void ts_lua_inject_cached_response_header_api(lua_State *L);

static int ts_lua_cached_response_header_get_status(lua_State *L);


void
ts_lua_inject_cached_response_api(lua_State *L)
{
    lua_newtable(L);

    ts_lua_inject_cached_response_header_api(L);

    lua_setfield(L, -2, "cached_response");
}

static void
ts_lua_inject_cached_response_header_api(lua_State *L)
{
    lua_newtable(L);                /*  .header */
    ts_lua_inject_cached_response_header_misc_api(L);
/*
    lua_createtable(L, 0, 2);

    lua_pushcfunction(L, ts_lua_cached_response_header_get);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ts_lua_cached_response_header_set);
    lua_setfield(L, -2, "__newindex");

    lua_setmetatable(L, -2);
*/
    lua_setfield(L, -2, "header");

    return;
}

static void
ts_lua_inject_cached_response_header_misc_api(lua_State *L)
{
    lua_pushcfunction(L, ts_lua_cached_response_header_get_status);
    lua_setfield(L, -2, "get_status");
}

static int
ts_lua_cached_response_header_get_status(lua_State *L)
{
    int              status;
    ts_lua_http_ctx  *http_ctx;

    http_ctx = ts_lua_get_http_ctx(L);

    if (!http_ctx->cached_response_hdrp) {
        if (TSHttpTxnCachedRespGet(http_ctx->txnp, &http_ctx->cached_response_bufp,
                    &http_ctx->cached_response_hdrp) != TS_SUCCESS) {

            lua_pushnil(L);
            return 1;
        }
    }

    status = TSHttpHdrStatusGet(http_ctx->cached_response_bufp, http_ctx->cached_response_hdrp);

    lua_pushinteger(L, status);

    return 1;
}

