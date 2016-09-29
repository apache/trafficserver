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
#include "ts_lua_constant.h"
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
#include "ts_lua_crypto.h"
#include "ts_lua_mgmt.h"
#include "ts_lua_package.h"
#include "ts_lua_stat.h"
#include "ts_lua_fetch.h"
#include "ts_lua_http_intercept.h"

static lua_State *ts_lua_new_state();
static void ts_lua_init_registry(lua_State *L);
static void ts_lua_init_globals(lua_State *L);
static void ts_lua_inject_ts_api(lua_State *L);

int
ts_lua_create_vm(ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

  for (i = 0; i < n; i++) {
    L = ts_lua_new_state();

    if (L == NULL)
      return -1;

    lua_pushvalue(L, LUA_GLOBALSINDEX);

    arr[i].gref   = luaL_ref(L, LUA_REGISTRYINDEX); /* L[REG][gref] = L[GLOBAL] */
    arr[i].lua    = L;
    arr[i].mutexp = TSMutexCreate();
  }

  return 0;
}

void
ts_lua_destroy_vm(ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

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
  lua_State *L;

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
ts_lua_add_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n, int argc, char *argv[], char *errbuf, int errbuf_size)
{
  int i, ret;
  int t;
  lua_State *L;

  for (i = 0; i < n; i++) {
    conf->_first = (i == 0) ? 1 : 0;
    conf->_last  = (i == n - 1) ? 1 : 0;

    TSMutexLock(arr[i].mutexp);

    L = arr[i].lua;

    lua_newtable(L);                                /* new TB1 */
    lua_pushvalue(L, -1);                           /* new TB2 */
    lua_setfield(L, -2, "_G");                      /* TB1[_G] = TB2 empty table, we can change _G to xx */
    lua_newtable(L);                                /* new TB3 */
    lua_rawgeti(L, LUA_REGISTRYINDEX, arr[i].gref); /* push L[GLOBAL] */
    lua_setfield(L, -2, "__index");                 /* TB3[__index] = L[GLOBAL] which has ts.xxx api */
    lua_setmetatable(L, -2);                        /* TB1[META]  = TB3 */
    lua_replace(L, LUA_GLOBALSINDEX);               /* L[GLOBAL] = TB1 */

    ts_lua_set_instance_conf(L, conf);

    if (conf->content) {
      if (luaL_loadstring(L, conf->content)) {
        snprintf(errbuf, errbuf_size - 1, "[%s] luaL_loadstring %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }

    } else if (strlen(conf->script)) {
      if (luaL_loadfile(L, conf->script)) {
        snprintf(errbuf, errbuf_size - 1, "[%s] luaL_loadfile %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }
    }

    if (lua_pcall(L, 0, 0, 0)) {
      snprintf(errbuf, errbuf_size - 1, "[%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      lua_pop(L, 1);
      TSMutexUnlock(arr[i].mutexp);
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
        snprintf(errbuf, errbuf_size - 1, "[%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        TSMutexUnlock(arr[i].mutexp);
        return -1;
      }

      ret = lua_tonumber(L, -1);
      lua_pop(L, 1);

      if (ret) {
        TSMutexUnlock(arr[i].mutexp);
        return -1; /* script parse error */
      }

    } else {
      lua_pop(L, 1); /* pop nil */
    }

    lua_pushlightuserdata(L, conf);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = L[GLOBAL] */

    lua_newtable(L);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = EMPTY */

    lua_gc(L, LUA_GCCOLLECT, 0);

    TSMutexUnlock(arr[i].mutexp);
  }

  return 0;
}

int
ts_lua_del_module(ts_lua_instance_conf *conf, ts_lua_main_ctx *arr, int n)
{
  int i;
  lua_State *L;

  for (i = 0; i < n; i++) {
    TSMutexLock(arr[i].mutexp);

    L = arr[i].lua;

    /* call "__clean__", to clean resources */
    lua_pushlightuserdata(L, conf);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = L[REG][conf] */

    lua_getglobal(L, "__clean__"); /* get __clean__ function */

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      if (lua_pcall(L, 0, 0, 0)) {
        TSError("[ts_lua][%s] lua_pcall %s failed: %s", __FUNCTION__, conf->script, lua_tostring(L, -1));
      }

    } else {
      lua_pop(L, 1); /* pop nil */
    }

    lua_pushlightuserdata(L, conf);
    lua_pushnil(L);
    lua_rawset(L, LUA_REGISTRYINDEX); /* L[REG][conf] = nil */

    lua_newtable(L);
    lua_replace(L, LUA_GLOBALSINDEX); /* L[GLOBAL] = EMPTY  */

    lua_gc(L, LUA_GCCOLLECT, 0);

    TSMutexUnlock(arr[i].mutexp);
  }

  return 0;
}

int
ts_lua_init_instance(ts_lua_instance_conf *conf ATS_UNUSED)
{
  return 0;
}

int
ts_lua_del_instance(ts_lua_instance_conf *conf ATS_UNUSED)
{
  return 0;
}

static void
ts_lua_init_registry(lua_State *L ATS_UNUSED)
{
  return;
}

static void
ts_lua_init_globals(lua_State *L)
{
  ts_lua_inject_ts_api(L);
}

static void
ts_lua_inject_ts_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_remap_api(L);
  ts_lua_inject_constant_api(L);

  ts_lua_inject_client_request_api(L);
  ts_lua_inject_server_request_api(L);
  ts_lua_inject_server_response_api(L);
  ts_lua_inject_client_response_api(L);
  ts_lua_inject_cached_response_api(L);
  ts_lua_inject_log_api(L);

  ts_lua_inject_context_api(L);
  ts_lua_inject_hook_api(L);

  ts_lua_inject_http_api(L);
  ts_lua_inject_intercept_api(L);
  ts_lua_inject_misc_api(L);
  ts_lua_inject_crypto_api(L);
  ts_lua_inject_mgmt_api(L);
  ts_lua_inject_package_api(L);
  ts_lua_inject_stat_api(L);
  ts_lua_inject_fetch_api(L);

  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, "ts");
  lua_pop(L, 2);

  lua_setglobal(L, "ts");
}

void
ts_lua_set_instance_conf(lua_State *L, ts_lua_instance_conf *conf)
{
  lua_pushliteral(L, "__ts_instance_conf");
  lua_pushlightuserdata(L, conf);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_instance_conf *
ts_lua_get_instance_conf(lua_State *L)
{
  ts_lua_instance_conf *conf;

  lua_pushliteral(L, "__ts_instance_conf");
  lua_rawget(L, LUA_GLOBALSINDEX);
  conf = lua_touserdata(L, -1);

  lua_pop(L, 1); // pop the conf out

  return conf;
}

void
ts_lua_set_cont_info(lua_State *L, ts_lua_cont_info *ci)
{
  lua_pushliteral(L, "__ts_cont_info");
  lua_pushlightuserdata(L, ci);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_cont_info *
ts_lua_get_cont_info(lua_State *L)
{
  ts_lua_cont_info *ci;

  lua_pushliteral(L, "__ts_cont_info");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ci = lua_touserdata(L, -1);

  lua_pop(L, 1); // pop the coroutine out

  return ci;
}

ts_lua_http_ctx *
ts_lua_create_async_ctx(lua_State *L, ts_lua_cont_info *hci, int n)
{
  int i;
  lua_State *l;
  ts_lua_coroutine *crt;
  ts_lua_http_ctx *actx;

  actx = TSmalloc(sizeof(ts_lua_http_ctx));
  memset(actx, 0, sizeof(ts_lua_http_ctx));

  // create lua_thread
  l = lua_newthread(L);

  // init the coroutine
  crt       = &actx->cinfo.routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = l;
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);

  // replace the param; start with 2 because first two params are not needed
  for (i = 2; i < n; i++) {
    lua_pushvalue(L, i + 1);
  }

  lua_xmove(L, l, n - 2);

  return actx;
}

void
ts_lua_destroy_async_ctx(ts_lua_http_ctx *http_ctx)
{
  ts_lua_cont_info *ci;

  ci = &http_ctx->cinfo;

  ts_lua_release_cont_info(ci);
  TSfree(http_ctx);
}

void
ts_lua_set_http_ctx(lua_State *L, ts_lua_http_ctx *ctx)
{
  lua_pushliteral(L, "__ts_http_ctx");
  lua_pushlightuserdata(L, ctx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_ctx *
ts_lua_get_http_ctx(lua_State *L)
{
  ts_lua_http_ctx *ctx;

  lua_pushliteral(L, "__ts_http_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ctx = lua_touserdata(L, -1);

  lua_pop(L, 1); // pop the ctx out

  return ctx;
}

ts_lua_http_ctx *
ts_lua_create_http_ctx(ts_lua_main_ctx *main_ctx, ts_lua_instance_conf *conf)
{
  ts_lua_coroutine *crt;
  ts_lua_http_ctx *http_ctx;
  lua_State *L;
  lua_State *l;

  L = main_ctx->lua;

  http_ctx = TSmalloc(sizeof(ts_lua_http_ctx));
  memset(http_ctx, 0, sizeof(ts_lua_http_ctx));

  // create coroutine for http_ctx
  crt = &http_ctx->cinfo.routine;
  l   = lua_newthread(L);

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

  // init coroutine
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
  crt->lua  = l;
  crt->mctx = main_ctx;

  http_ctx->instance_conf = conf;

  ts_lua_set_http_ctx(l, http_ctx);
  ts_lua_create_context_table(l);

  return http_ctx;
}

void
ts_lua_destroy_http_ctx(ts_lua_http_ctx *http_ctx)
{
  ts_lua_cont_info *ci;

  ci = &http_ctx->cinfo;

  if (!http_ctx->remap) {
    if (http_ctx->client_request_bufp) {
      TSHandleMLocRelease(http_ctx->client_request_bufp, TS_NULL_MLOC, http_ctx->client_request_hdrp);
    }
  }

  if (http_ctx->server_request_url) {
    TSHandleMLocRelease(http_ctx->server_request_bufp, http_ctx->server_request_hdrp, http_ctx->server_request_url);
  }

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
    TSMimeHdrDestroy(http_ctx->cached_response_bufp, http_ctx->cached_response_hdrp);
    TSHandleMLocRelease(http_ctx->cached_response_bufp, TS_NULL_MLOC, http_ctx->cached_response_hdrp);
    TSMBufferDestroy(http_ctx->cached_response_bufp);
  }

  ts_lua_release_cont_info(ci);
  TSfree(http_ctx);
}

void
ts_lua_set_http_intercept_ctx(lua_State *L, ts_lua_http_intercept_ctx *ictx)
{
  lua_pushliteral(L, "__ts_http_intercept_ctx");
  lua_pushlightuserdata(L, ictx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_intercept_ctx *
ts_lua_get_http_intercept_ctx(lua_State *L)
{
  ts_lua_http_intercept_ctx *ictx;

  lua_pushliteral(L, "__ts_http_intercept_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  ictx = lua_touserdata(L, -1);

  lua_pop(L, 1); // pop the ictx out

  return ictx;
}

ts_lua_http_intercept_ctx *
ts_lua_create_http_intercept_ctx(lua_State *L, ts_lua_http_ctx *http_ctx, int n)
{
  int i;
  lua_State *l;
  ts_lua_cont_info *hci;
  ts_lua_coroutine *crt;
  ts_lua_http_intercept_ctx *ictx;

  hci = &http_ctx->cinfo;

  ictx = TSmalloc(sizeof(ts_lua_http_intercept_ctx));
  memset(ictx, 0, sizeof(ts_lua_http_intercept_ctx));

  ictx->hctx = http_ctx;

  // create lua_thread
  l = lua_newthread(L);

  // init the coroutine
  crt       = &ictx->cinfo.routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = l;
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);

  // Todo: replace the global, context table for crt->lua

  // replicate the param
  for (i = 0; i < n; i++) {
    lua_pushvalue(L, i + 1);
  }

  lua_xmove(L, l, n); // move the intercept function and params to the new lua_thread

  ts_lua_set_http_intercept_ctx(l, ictx);

  return ictx;
}

void
ts_lua_destroy_http_intercept_ctx(ts_lua_http_intercept_ctx *ictx)
{
  ts_lua_cont_info *ci;

  ci = &ictx->cinfo;

  if (ictx->net_vc) {
    TSVConnClose(ictx->net_vc);
  }

  TS_LUA_RELEASE_IO_HANDLE((&ictx->input));
  TS_LUA_RELEASE_IO_HANDLE((&ictx->output));

  ts_lua_release_cont_info(ci);
  TSfree(ictx);
}

void
ts_lua_set_http_transform_ctx(lua_State *L, ts_lua_http_transform_ctx *tctx)
{
  lua_pushliteral(L, "__ts_http_transform_ctx");
  lua_pushlightuserdata(L, tctx);
  lua_rawset(L, LUA_GLOBALSINDEX);
}

ts_lua_http_transform_ctx *
ts_lua_get_http_transform_ctx(lua_State *L)
{
  ts_lua_http_transform_ctx *tctx;

  lua_pushliteral(L, "__ts_http_transform_ctx");
  lua_rawget(L, LUA_GLOBALSINDEX);
  tctx = lua_touserdata(L, -1);

  lua_pop(L, 1); // pop the ictx out

  return tctx;
}

ts_lua_http_transform_ctx *
ts_lua_create_http_transform_ctx(ts_lua_http_ctx *http_ctx, TSVConn connp)
{
  lua_State *L;
  ts_lua_cont_info *hci;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;
  ts_lua_http_transform_ctx *transform_ctx;

  hci = &http_ctx->cinfo;
  L   = hci->routine.lua;

  transform_ctx = (ts_lua_http_transform_ctx *)TSmalloc(sizeof(ts_lua_http_transform_ctx));
  memset(transform_ctx, 0, sizeof(ts_lua_http_transform_ctx));

  transform_ctx->hctx = http_ctx;
  TSContDataSet(connp, transform_ctx);

  ci        = &transform_ctx->cinfo;
  ci->contp = connp;
  ci->mutex = TSContMutexGet((TSCont)http_ctx->txnp);

  crt       = &ci->routine;
  crt->mctx = hci->routine.mctx;
  crt->lua  = lua_newthread(L);
  crt->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
  ts_lua_set_http_transform_ctx(crt->lua, transform_ctx);

  lua_pushlightuserdata(L, transform_ctx);
  lua_pushvalue(L, 2);
  lua_rawset(L, LUA_GLOBALSINDEX); // L[GLOBAL][transform_ctx] = transform handler

  return transform_ctx;
}

void
ts_lua_destroy_http_transform_ctx(ts_lua_http_transform_ctx *transform_ctx)
{
  ts_lua_cont_info *ci;

  if (!transform_ctx)
    return;

  ci = &transform_ctx->cinfo;

  TS_LUA_RELEASE_IO_HANDLE((&transform_ctx->output));
  TS_LUA_RELEASE_IO_HANDLE((&transform_ctx->reserved));

  ts_lua_release_cont_info(ci);

  TSfree(transform_ctx);
}

int
ts_lua_http_cont_handler(TSCont contp, TSEvent ev, void *edata)
{
  TSHttpTxn txnp;
  int event, ret, rc, n, t;
  lua_State *L;
  ts_lua_http_ctx *http_ctx;
  ts_lua_main_ctx *main_ctx;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;

  event    = (int)ev;
  http_ctx = (ts_lua_http_ctx *)TSContDataGet(contp);
  ci       = &http_ctx->cinfo;
  crt      = &ci->routine;

  main_ctx = crt->mctx;
  L        = crt->lua;

  txnp = http_ctx->txnp;

  rc = ret = 0;

  TSMutexLock(main_ctx->mutexp);
  ts_lua_set_cont_info(L, ci);

  switch (event) {
  case TS_EVENT_HTTP_POST_REMAP:

    lua_getglobal(L, TS_LUA_FUNCTION_POST_REMAP);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:

    lua_getglobal(L, TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_SEND_REQUEST);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_READ_RESPONSE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:

    // client response can be changed within a transaction
    // (e.g. due to the follow redirect feature). So, clearing the pointers
    // to allow API(s) to fetch the pointers again when it re-enters the hook
    if (http_ctx->client_response_hdrp != NULL) {
      TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
      http_ctx->client_response_hdrp = NULL;
    }

    lua_getglobal(L, TS_LUA_FUNCTION_SEND_RESPONSE);

    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_READ_REQUEST);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_TXN_START:

    lua_getglobal(L, TS_LUA_FUNCTION_TXN_START);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_PRE_REMAP:

    lua_getglobal(L, TS_LUA_FUNCTION_PRE_REMAP);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_OS_DNS:

    lua_getglobal(L, TS_LUA_FUNCTION_OS_DNS);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_SELECT_ALT:

    lua_getglobal(L, TS_LUA_FUNCTION_SELECT_ALT);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:

    lua_getglobal(L, TS_LUA_FUNCTION_READ_CACHE);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      ret = lua_resume(L, 0);
    }

    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    lua_getglobal(L, TS_LUA_FUNCTION_TXN_CLOSE);
    if (lua_type(L, -1) == LUA_TFUNCTION) {
      if (lua_pcall(L, 0, 1, 0)) {
        TSError("[ts_lua] lua_pcall failed: %s", lua_tostring(L, -1));
      }
    }

    ts_lua_destroy_http_ctx(http_ctx);
    break;

  case TS_LUA_EVENT_COROUTINE_CONT:
    n   = (intptr_t)edata;
    ret = lua_resume(L, n);

  default:
    break;
  }

  switch (ret) {
  case 0: // coroutine succeed
    t = lua_gettop(L);
    if (t > 0) {
      rc = lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    break;

  case LUA_YIELD: // coroutine yield
    rc = 1;
    break;

  default: // coroutine failed
    TSError("[ts_lua] lua_resume failed: %s", lua_tostring(L, -1));
    rc = -1;
    lua_pop(L, 1);
    break;
  }

  TSMutexUnlock(main_ctx->mutexp);

  if (rc == 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  } else if (rc < 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);

  } else {
    // wait for async
  }

  return 0;
}
