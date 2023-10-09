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

#include "tscore/ink_platform.h"
#include <netinet/in.h>
#include "ts_lua_util.h"

static int ts_lua_get_process_id(lua_State *L);
static int ts_lua_get_now_time(lua_State *L);
static int ts_lua_debug(lua_State *L);
static int ts_lua_error(lua_State *L);
static int ts_lua_sleep(lua_State *L);
static int ts_lua_host_lookup(lua_State *L);
static int ts_lua_schedule(lua_State *L);
static int ts_lua_get_install_dir(lua_State *L);
static int ts_lua_get_config_dir(lua_State *L);
static int ts_lua_get_runtime_dir(lua_State *L);
static int ts_lua_get_plugin_dir(lua_State *L);
static int ts_lua_get_traffic_server_version(lua_State *L);

static int ts_lua_sleep_cleanup(ts_lua_async_item *ai);
static int ts_lua_sleep_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_host_lookup_cleanup(ts_lua_async_item *ai);
static int ts_lua_host_lookup_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_schedule_handler(TSCont contp, TSEvent event, void *edata);

static void ts_lua_inject_misc_variables(lua_State *L);

void
ts_lua_inject_misc_api(lua_State *L)
{
  lua_newtable(L);

  /* ts.process.uuid() */
  lua_pushcfunction(L, ts_lua_get_process_id);
  lua_setfield(L, -2, "uuid");

  lua_setfield(L, -2, "process");

  /* ts.now() */
  lua_pushcfunction(L, ts_lua_get_now_time);
  lua_setfield(L, -2, "now");

  /* ts.debug(...) */
  lua_pushcfunction(L, ts_lua_debug);
  lua_setfield(L, -2, "debug");

  /* ts.error(...) */
  lua_pushcfunction(L, ts_lua_error);
  lua_setfield(L, -2, "error");

  /* ts.sleep(...) */
  lua_pushcfunction(L, ts_lua_sleep);
  lua_setfield(L, -2, "sleep");

  /* ts.schedule(...) */
  lua_pushcfunction(L, ts_lua_schedule);
  lua_setfield(L, -2, "schedule");

  /* ts.host_lookup(...) */
  lua_pushcfunction(L, ts_lua_host_lookup);
  lua_setfield(L, -2, "host_lookup");

  /* ts.get_install_dir(...) */
  lua_pushcfunction(L, ts_lua_get_install_dir);
  lua_setfield(L, -2, "get_install_dir");

  /* ts.get_config_dir(...) */
  lua_pushcfunction(L, ts_lua_get_config_dir);
  lua_setfield(L, -2, "get_config_dir");

  /* ts.get_runtime_dir(...) */
  lua_pushcfunction(L, ts_lua_get_runtime_dir);
  lua_setfield(L, -2, "get_runtime_dir");

  /* ts.get_plugin_dir(...) */
  lua_pushcfunction(L, ts_lua_get_plugin_dir);
  lua_setfield(L, -2, "get_plugin_dir");

  /* ts.get_traffic_server_version(...) */
  lua_pushcfunction(L, ts_lua_get_traffic_server_version);
  lua_setfield(L, -2, "get_traffic_server_version");

  ts_lua_inject_misc_variables(L);
}

static void
ts_lua_inject_misc_variables(lua_State *L)
{
  lua_pushinteger(L, TS_THREAD_POOL_NET);
  lua_setglobal(L, "TS_LUA_THREAD_POOL_NET");
  lua_pushinteger(L, TS_THREAD_POOL_TASK);
  lua_setglobal(L, "TS_LUA_THREAD_POOL_TASK");
}

static int
ts_lua_get_process_id(lua_State *L)
{
  const char *s;
  TSUuid process = TSProcessUuidGet();
  if (process) {
    s = TSUuidStringGet(process);
  } else {
    return luaL_error(L, "not able to get process uuid");
  }
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_now_time(lua_State *L)
{
  lua_Number now;

  // Return fractional seconds.
  now = ((lua_Number)TShrtime()) / 1000000000.0;
  lua_pushnumber(L, now);
  return 1;
}

static int
ts_lua_debug(lua_State *L)
{
  const char *msg;
  const char *flag;
  size_t msg_len = 0, flag_len = 0;

  if (lua_gettop(L) == 2) {
    msg  = luaL_checklstring(L, 2, &msg_len);
    flag = luaL_checklstring(L, 1, &flag_len);
    TSDebug(flag, "%.*s", (int)msg_len, msg);
  } else {
    msg = luaL_checklstring(L, 1, &msg_len);
    TSDebug(TS_LUA_DEBUG_TAG, "%.*s", (int)msg_len, msg);
  }

  return 0;
}

static int
ts_lua_error(lua_State *L)
{
  const char *msg;
  size_t len = 0;

  msg = luaL_checklstring(L, 1, &len);
  TSError("%.*s", (int)len, msg);
  return 0;
}

static int
ts_lua_schedule(lua_State *L)
{
  int sec;
  int type;
  int entry;

  ts_lua_http_ctx *actx;

  int n;

  TSCont contp;
  ts_lua_cont_info *ci;
  ts_lua_cont_info *nci;

  ci = ts_lua_get_cont_info(L);
  if (ci == NULL) {
    TSError("[ts_lua][%s] no cont info found", __FUNCTION__);
    TSReleaseAssert(!"Unexpected fetch of cont info");
    return 0;
  }

  entry = lua_tointeger(L, 1);

  sec = luaL_checknumber(L, 2);
  if (sec < 1) {
    sec = 0;
  }

  type = lua_type(L, 3);
  if (type != LUA_TFUNCTION)
    return 0;

  n = lua_gettop(L);

  if (n < 3) {
    TSError("[ts_lua] ts.schedule need at least three parameters");
    return 0;
  }

  // TO-DO unset the original context in L
  actx = ts_lua_create_async_ctx(L, ci, n);

  contp = TSContCreate(ts_lua_schedule_handler, ci->mutex);
  TSContDataSet(contp, actx);

  nci        = &actx->cinfo;
  nci->contp = contp;
  nci->mutex = ci->mutex;

  TSContSchedule(contp, sec * 1000, entry);

  return 0;
}

static int
ts_lua_schedule_handler(TSCont contp, TSEvent ev, void *edata)
{
  lua_State *L;
  ts_lua_cont_info *ci;
  ts_lua_coroutine *crt;
  int event, n, ret;
  ts_lua_http_ctx *actx;
  ts_lua_main_ctx *main_ctx;

  event = (int)ev;
  TSDebug(TS_LUA_DEBUG_TAG, "getting actx and other info");
  actx = (ts_lua_http_ctx *)TSContDataGet(contp);

  TSDebug(TS_LUA_DEBUG_TAG, "getting http_Ctx");
  ci  = &actx->cinfo;
  crt = &ci->routine;

  main_ctx = crt->mctx;
  L        = crt->lua;

  TSMutexLock(main_ctx->mutexp);
  ts_lua_set_cont_info(L, ci);

  if (event == TS_LUA_EVENT_COROUTINE_CONT) {
    TSDebug(TS_LUA_DEBUG_TAG, "event is coroutine_cont");
    n   = (intptr_t)edata;
    ret = lua_resume(L, n);
  } else {
    TSDebug(TS_LUA_DEBUG_TAG, "event is not coroutine_cont");
    n   = lua_gettop(L);
    ret = lua_resume(L, n - 1);
  }

  if (ret == LUA_YIELD) {
    TSMutexUnlock(main_ctx->mutexp);
    goto done;
  }

  if (ret != 0) {
    TSError("[ts_lua] lua_resume failed: %s", lua_tostring(L, -1));
  }

  lua_pop(L, lua_gettop(L));
  TSMutexUnlock(main_ctx->mutexp);
  ts_lua_destroy_async_ctx(actx);

done:
  return 0;
}

static int
ts_lua_sleep(lua_State *L)
{
  int sec;
  TSAction action;
  TSCont contp;
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ci = ts_lua_get_cont_info(L);
  if (ci == NULL) {
    TSError("[ts_lua][%s] no cont info found", __FUNCTION__);
    TSReleaseAssert(!"Unexpected fetch of cont info");
    return 0;
  }

  sec = luaL_checknumber(L, 1);
  if (sec < 1) {
    sec = 1;
  }

  contp  = TSContCreate(ts_lua_sleep_handler, ci->mutex);
  action = TSContSchedule(contp, sec * 1000, TS_THREAD_POOL_DEFAULT);

  ai = ts_lua_async_create_item(contp, ts_lua_sleep_cleanup, (void *)action, ci);
  TSContDataSet(contp, ai);

  return lua_yield(L, 0);
}

static int
ts_lua_sleep_handler(TSCont contp, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ai = TSContDataGet(contp);
  ci = ai->cinfo;

  ai->data = NULL;
  ts_lua_sleep_cleanup(ai);

  TSContCall(ci->contp, TS_LUA_EVENT_COROUTINE_CONT, 0);

  return 0;
}

static int
ts_lua_sleep_cleanup(ts_lua_async_item *ai)
{
  if (ai->data) {
    TSActionCancel((TSAction)ai->data);
    ai->data = NULL;
  }

  TSContDestroy(ai->contp);
  ai->deleted = 1;

  return 0;
}

static int
ts_lua_host_lookup(lua_State *L)
{
  const char *host;
  size_t host_len = 0;
  TSAction action;
  TSCont contp;
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ci = ts_lua_get_cont_info(L);
  if (ci == NULL) {
    TSError("[ts_lua][%s] no cont info found", __FUNCTION__);
    TSReleaseAssert(!"Unexpected fetch of cont info");
    return 0;
  }

  if (lua_gettop(L) != 1) {
    TSError("[ts_lua] ts.host_lookup need at least one parameter");
    return 0;
  }

  host = luaL_checklstring(L, 1, &host_len);

  contp  = TSContCreate(ts_lua_host_lookup_handler, ci->mutex);
  action = TSHostLookup(contp, host, host_len);

  ai = ts_lua_async_create_item(contp, ts_lua_host_lookup_cleanup, (void *)action, ci);
  TSContDataSet(contp, ai);

  return lua_yield(L, 0);
}

static int
ts_lua_host_lookup_handler(TSCont contp, TSEvent event, void *edata)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;
  struct sockaddr const *addr;
  char cip[128];
  lua_State *L;
  ts_lua_coroutine *crt;

  ai  = TSContDataGet(contp);
  ci  = ai->cinfo;
  crt = &ci->routine;
  L   = crt->lua;

  ai->data = NULL;
  ts_lua_host_lookup_cleanup(ai);

  if (event != TS_EVENT_HOST_LOOKUP) {
    TSError("[ts_lua] ts.host_lookup receives unknown event");
    lua_pushnil(L);
  } else if (!edata) {
    lua_pushnil(L);
  } else {
    TSHostLookupResult result = (TSHostLookupResult)edata;
    addr                      = TSHostLookupResultAddrGet(result);
    if (addr->sa_family == AF_INET) {
      inet_ntop(AF_INET, (const void *)&((struct sockaddr_in *)addr)->sin_addr, cip, sizeof(cip));
    } else {
      inet_ntop(AF_INET6, (const void *)&((struct sockaddr_in6 *)addr)->sin6_addr, cip, sizeof(cip));
    }
    lua_pushstring(L, cip);
  }

  TSContCall(ci->contp, TS_LUA_EVENT_COROUTINE_CONT, (void *)1);

  return 0;
}

static int
ts_lua_host_lookup_cleanup(ts_lua_async_item *ai)
{
  if (ai->data) {
    TSActionCancel((TSAction)ai->data);
    ai->data = NULL;
  }

  TSContDestroy(ai->contp);
  ai->deleted = 1;

  return 0;
}

static int
ts_lua_get_install_dir(lua_State *L)
{
  const char *s = TSInstallDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_config_dir(lua_State *L)
{
  const char *s = TSConfigDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_runtime_dir(lua_State *L)
{
  const char *s = TSRuntimeDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_plugin_dir(lua_State *L)
{
  const char *s = TSPluginDirGet();
  lua_pushstring(L, s);
  return 1;
}

static int
ts_lua_get_traffic_server_version(lua_State *L)
{
  const char *s = TSTrafficServerVersionGet();
  lua_pushstring(L, s);
  return 1;
}
