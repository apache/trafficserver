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
#include "ts_lua_client_request.h"
#include "ts_lua_server_request.h"
#include "ts_lua_server_response.h"
#include "ts_lua_client_response.h"
#include "ts_lua_cached_response.h"
#include "ts_lua_context.h"
#include "ts_lua_hook.h"
#include "ts_lua_http.h"
#include "ts_lua_misc.h"
#include "ts_lua_log.h"

static lua_State * ts_lua_new_state();
static void ts_lua_init_registry(lua_State *L);
static void ts_lua_init_globals(lua_State *L);
static void ts_lua_inject_ts_api(lua_State *L);


int
ts_lua_create_vm(ts_lua_main_ctx *arr, int n)
{
    int         i;
    lua_State   *L;

    for (i = 0; i < n; i++) {

        L = ts_lua_new_state();

        if (L == NULL)
            return -1;

        lua_pushvalue(L, LUA_GLOBALSINDEX);

        arr[i].gref = luaL_ref(L, LUA_REGISTRYINDEX);
        arr[i].lua = L;
        arr[i].mutexp = TSMutexCreate();
    }

    return 0;
}

void
ts_lua_destroy_vm(ts_lua_main_ctx *arr, int n)
{
    int         i;
    lua_State   *L;

    for (i = 0; i < n; i++) {

        L = arr[i].lua;
        if (L)
            lua_close(L);
    }

    return;
}

lua_State *
ts_lua_new_state()
{
    lua_State   *L;

    L = luaL_newstate();

    if (L == NULL) {
        return NULL;
    }

    luaL_openlibs(L);

    ts_lua_init_registry(L);

    ts_lua_init_globals(L);

    return L;
}

int
ts_lua_add_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n, int argc, char *argv[])
{
    int             i, ret;
    int             t;
    lua_State       *L;

    for (i = 0; i < n; i++) {

        L = arr[i].lua;

        lua_newtable(L);                                    // create this module's global table
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "_G");
        lua_newtable(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, arr[i].gref);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        lua_replace(L, LUA_GLOBALSINDEX);

        if (luaL_loadfile(L, conf->script)) {
            fprintf(stderr, "[%s] luaL_loadfile %s failed: %s\n", __FUNCTION__, conf->script, lua_tostring(L, -1));
            lua_pop(L, 1);
            return -1;
        }

        if (lua_pcall(L, 0, 0, 0)) {
            fprintf(stderr, "[%s] lua_pcall %s failed: %s\n", __FUNCTION__, conf->script, lua_tostring(L, -1));
            lua_pop(L, 1);
            return -1;
        }

        /* call "__init__", to parse parameters */
        lua_getglobal(L, "__init__");

        if (lua_type(L, -1) == LUA_TFUNCTION) {

            lua_newtable(L);

            for (t = 0; t < argc; t++) {
                lua_pushnumber(L, t);
                lua_pushstring(L, argv[t]);
                lua_rawset(L, -3);
            }

            if (lua_pcall(L, 1, 1, 0)) {
                fprintf(stderr, "[%s] lua_pcall %s failed: %s\n", __FUNCTION__, conf->script, lua_tostring(L, -1));
                lua_pop(L, 1);
                return -1;
            }

            ret = lua_tonumber(L, -1);
            lua_pop(L, 1);

            if (ret)
                return -1;          /* script parse error */

        } else {
            lua_pop(L, 1);          /* pop nil */
        }


        lua_pushlightuserdata(L, conf);
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_rawset(L, LUA_REGISTRYINDEX);

        lua_newtable(L);
        lua_replace(L, LUA_GLOBALSINDEX);               // set empty table to global
    }


    return 0;
}


static
void ts_lua_init_registry(lua_State *L ATS_UNUSED)
{
    return;
}

static
void ts_lua_init_globals(lua_State *L)
{
    ts_lua_inject_ts_api(L);
}

static void
ts_lua_inject_ts_api(lua_State *L)
{
    lua_newtable(L);

    ts_lua_inject_remap_api(L);

    ts_lua_inject_client_request_api(L);
    ts_lua_inject_server_request_api(L);
    ts_lua_inject_server_response_api(L);
    ts_lua_inject_client_response_api(L);
    ts_lua_inject_cached_response_api(L);
    ts_lua_inject_log_api(L);

    ts_lua_inject_context_api(L);
    ts_lua_inject_hook_api(L);

    ts_lua_inject_http_api(L);
    ts_lua_inject_misc_api(L);

    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, "ts");
    lua_pop(L, 2);

    lua_setglobal(L, "ts");
}

void
ts_lua_set_http_ctx(lua_State *L, ts_lua_http_ctx  *ctx)
{
    lua_pushliteral(L, "__ts_http_ctx");
    lua_pushlightuserdata(L, ctx);
    lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_ctx *
ts_lua_get_http_ctx(lua_State *L)
{
    ts_lua_http_ctx  *ctx;

    lua_pushliteral(L, "__ts_http_ctx");
    lua_rawget(L, LUA_GLOBALSINDEX);
    ctx = lua_touserdata(L, -1);

    lua_pop(L, 1);                      // pop the ctx out

    return ctx;
}

ts_lua_http_ctx *
ts_lua_create_http_ctx(ts_lua_main_ctx *main_ctx, ts_lua_instance_conf *conf)
{
    ts_lua_http_ctx     *http_ctx;
    lua_State           *L;
    lua_State           *l;

    L = main_ctx->lua;

    http_ctx = TSmalloc(sizeof(ts_lua_http_ctx));
    memset(http_ctx, 0, sizeof(ts_lua_http_ctx));

    http_ctx->lua = lua_newthread(L);
    l = http_ctx->lua;

    lua_pushlightuserdata(L, conf);
    lua_rawget(L, LUA_REGISTRYINDEX);

    /* new globals table for coroutine */
    lua_newtable(l);
    lua_pushvalue(l, -1);
    lua_setfield(l, -2, "_G"); 
    lua_newtable(l);
    lua_xmove(L, l, 1);
    lua_setfield(l, -2, "__index");
    lua_setmetatable(l, -2);

    lua_replace(l, LUA_GLOBALSINDEX);

    http_ctx->ref = luaL_ref(L, LUA_REGISTRYINDEX);

    http_ctx->mctx = main_ctx;

    ts_lua_set_http_ctx(http_ctx->lua, http_ctx);
    ts_lua_create_context_table(http_ctx->lua);

    return http_ctx;
}


void
ts_lua_destroy_http_ctx(ts_lua_http_ctx* http_ctx)
{
    ts_lua_main_ctx   *main_ctx;

    main_ctx = http_ctx->mctx;

    if (http_ctx->server_request_bufp) {
        TSHandleMLocRelease(http_ctx->server_request_bufp, TS_NULL_MLOC, http_ctx->server_request_hdrp);
    }

    if (http_ctx->server_response_bufp) {
        TSHandleMLocRelease(http_ctx->server_response_bufp, TS_NULL_MLOC, http_ctx->server_response_hdrp);
    }

    if (http_ctx->client_response_bufp) {
        TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
    }

    if (http_ctx->cached_response_bufp) {
        TSHandleMLocRelease(http_ctx->cached_response_bufp, TS_NULL_MLOC, http_ctx->cached_response_hdrp);
    }

    luaL_unref(main_ctx->lua, LUA_REGISTRYINDEX, http_ctx->ref);
    TSfree(http_ctx);
}

void
ts_lua_set_http_intercept_ctx(lua_State *L, ts_lua_http_intercept_ctx  *ictx)
{
    lua_pushliteral(L, "__ts_http_intercept_ctx");
    lua_pushlightuserdata(L, ictx);
    lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_intercept_ctx *
ts_lua_get_http_intercept_ctx(lua_State *L)
{
    ts_lua_http_intercept_ctx  *ictx;

    lua_pushliteral(L, "__ts_http_intercept_ctx");
    lua_rawget(L, LUA_GLOBALSINDEX);
    ictx = lua_touserdata(L, -1);

    lua_pop(L, 1);                      // pop the ictx out

    return ictx;
}

ts_lua_http_intercept_ctx *
ts_lua_create_http_intercept_ctx(ts_lua_http_ctx *http_ctx)
{
    lua_State           *L;
    ts_lua_http_intercept_ctx   *ictx;

    L = http_ctx->lua;

    ictx = TSmalloc(sizeof(ts_lua_http_intercept_ctx));
    memset(ictx, 0, sizeof(ts_lua_http_intercept_ctx));

    ictx->lua = lua_newthread(L);

    ictx->ref = luaL_ref(L, LUA_REGISTRYINDEX);

    ictx->hctx = http_ctx;

    ts_lua_set_http_intercept_ctx(ictx->lua, ictx);

    return ictx;
}

void 
ts_lua_destroy_http_intercept_ctx(ts_lua_http_intercept_ctx *ictx)
{
    ts_lua_http_ctx   *http_ctx;

    http_ctx = ictx->hctx;

    if (ictx->net_vc)
        TSVConnClose(ictx->net_vc);

    TS_LUA_RELEASE_IO_HANDLE((&ictx->input));
    TS_LUA_RELEASE_IO_HANDLE((&ictx->output));

    luaL_unref(http_ctx->lua, LUA_REGISTRYINDEX, ictx->ref);
    TSfree(ictx);
    return;
}

void 
ts_lua_destroy_transform_ctx(ts_lua_transform_ctx *transform_ctx)
{
    if (!transform_ctx)
        return;

    if (transform_ctx->output_reader)
        TSIOBufferReaderFree(transform_ctx->output_reader);

    if (transform_ctx->output_buffer)
        TSIOBufferDestroy(transform_ctx->output_buffer);

    TSfree(transform_ctx);
}

int
ts_lua_http_cont_handler(TSCont contp, TSEvent event, void *edata)
{
    TSHttpTxn           txnp = (TSHttpTxn)edata;
    lua_State           *l;
    ts_lua_http_ctx     *http_ctx;
    ts_lua_main_ctx     *main_ctx;

    http_ctx = (ts_lua_http_ctx*)TSContDataGet(contp);
    main_ctx = http_ctx->mctx;

    l = http_ctx->lua;

    TSMutexLock(main_ctx->mutexp);

    switch (event) {

        case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:

            lua_getglobal(l, TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE);
            if (lua_type(l, -1) == LUA_TFUNCTION) {
                if (lua_pcall(l, 0, 1, 0)) {
                    fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
                }
            }

            break;

        case TS_EVENT_HTTP_SEND_REQUEST_HDR:

            lua_getglobal(l, TS_LUA_FUNCTION_SEND_REQUEST);
            if (lua_type(l, -1) == LUA_TFUNCTION) {
                if (lua_pcall(l, 0, 1, 0)) {
                    fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
                }
            }

            break;

        case TS_EVENT_HTTP_READ_RESPONSE_HDR:

            lua_getglobal(l, TS_LUA_FUNCTION_READ_RESPONSE);
            if (lua_type(l, -1) == LUA_TFUNCTION) {
                if (lua_pcall(l, 0, 1, 0)) {
                    fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
                }
            }

            break;

        case TS_EVENT_HTTP_SEND_RESPONSE_HDR:

            lua_getglobal(l, TS_LUA_FUNCTION_SEND_RESPONSE);
            if (lua_type(l, -1) == LUA_TFUNCTION) {
                if (lua_pcall(l, 0, 1, 0)) {
                    fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
                }
            }

            break;

        case TS_EVENT_HTTP_TXN_CLOSE:
            ts_lua_destroy_http_ctx(http_ctx);
            TSContDestroy(contp);
            break;

        default:
            break;
    }

    TSMutexUnlock(main_ctx->mutexp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
}

