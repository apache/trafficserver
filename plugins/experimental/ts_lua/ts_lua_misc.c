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


static int ts_lua_get_now_time(lua_State *L);
static int ts_lua_debug(lua_State *L);
static int ts_lua_error(lua_State *L);
static int ts_lua_sleep(lua_State *L);
static int ts_lua_say(lua_State *L);
static int ts_lua_flush(lua_State *L);

static int ts_lua_sleep_cleanup(struct ict_item *item);
static int ts_lua_sleep_handler(TSCont contp, TSEvent event, void *edata);

int ts_lua_flush_launch(ts_lua_http_intercept_ctx *ictx);
static int ts_lua_flush_cleanup(struct ict_item *item);
static int ts_lua_flush_handler(TSCont contp, TSEvent event, void *edata);


void
ts_lua_inject_misc_api(lua_State *L)
{
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

  /* ts.say(...) */
  lua_pushcfunction(L, ts_lua_say);
  lua_setfield(L, -2, "say");

  /* ts.flush(...) */
  lua_pushcfunction(L, ts_lua_flush);
  lua_setfield(L, -2, "flush");
}

static int
ts_lua_get_now_time(lua_State *L)
{
  time_t now;

  now = TShrtime() / 1000000000;
  lua_pushnumber(L, now);
  return 1;
}

static int
ts_lua_debug(lua_State *L)
{
  const char *msg;

  msg = luaL_checkstring(L, 1);
  TSDebug(TS_LUA_DEBUG_TAG, msg, NULL);
  return 0;
}

static int
ts_lua_error(lua_State *L)
{
  const char *msg;

  msg = luaL_checkstring(L, 1);
  TSError(msg, NULL);
  return 0;
}

static int
ts_lua_sleep(lua_State *L)
{
  int sec;
  TSAction action;
  TSCont contp;
  ts_lua_http_intercept_item *node;
  ts_lua_http_intercept_ctx *ictx;

  ictx = ts_lua_get_http_intercept_ctx(L);
  sec = luaL_checknumber(L, 1);

  contp = TSContCreate(ts_lua_sleep_handler, TSContMutexGet(ictx->contp));
  action = TSContSchedule(contp, sec * 1000, TS_THREAD_POOL_DEFAULT);

  node = (ts_lua_http_intercept_item *)TSmalloc(sizeof(ts_lua_http_intercept_item));
  TS_LUA_ADD_INTERCEPT_ITEM(ictx, node, contp, ts_lua_sleep_cleanup, action);
  TSContDataSet(contp, node);

  return lua_yield(L, 0);
}

static int
ts_lua_say(lua_State *L)
{
  const char *data;
  size_t len;

  ts_lua_http_intercept_ctx *ictx;

  ictx = ts_lua_get_http_intercept_ctx(L);

  data = luaL_checklstring(L, 1, &len);

  if (len > 0) {
    TSIOBufferWrite(ictx->output.buffer, data, len);
    TSVIOReenable(ictx->output.vio);
  }

  return 0;
}

static int
ts_lua_flush(lua_State *L)
{
  int64_t avail;
  ts_lua_http_intercept_ctx *ictx;

  ictx = ts_lua_get_http_intercept_ctx(L);
  avail = TSIOBufferReaderAvail(ictx->output.reader);

  if (avail > 0) {
    ictx->to_flush = TSVIONDoneGet(ictx->output.vio) + TSIOBufferReaderAvail(ictx->output.reader);
    TSVIOReenable(ictx->output.vio);

    return lua_yield(L, 0);
  }

  return 0;
}

static int
ts_lua_sleep_handler(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  ts_lua_http_intercept_item *item = TSContDataGet(contp);

  ts_lua_sleep_cleanup(item);

  TSContCall(item->ictx->contp, event, 0);

  return 0;
}

static int
ts_lua_sleep_cleanup(struct ict_item *item)
{
  if (item->deleted)
    return 0;

  if (item->data) {
    TSActionCancel((TSAction)item->data);
    item->data = NULL;
  }

  TSContDestroy(item->contp);
  item->deleted = 1;

  return 0;
}

int
ts_lua_flush_launch(ts_lua_http_intercept_ctx *ictx)
{
  TSAction action;
  TSCont contp;
  ts_lua_http_intercept_item *node;

  contp = TSContCreate(ts_lua_flush_handler, TSContMutexGet(ictx->contp));
  action = TSContSchedule(contp, 0, TS_THREAD_POOL_DEFAULT);

  node = (ts_lua_http_intercept_item *)TSmalloc(sizeof(ts_lua_http_intercept_item));
  TS_LUA_ADD_INTERCEPT_ITEM(ictx, node, contp, ts_lua_flush_cleanup, action);
  TSContDataSet(contp, node);

  return 0;
}

static int
ts_lua_flush_cleanup(struct ict_item *item)
{
  if (item->deleted)
    return 0;

  if (item->data) {
    TSActionCancel((TSAction)item->data);
    item->data = NULL;
  }

  TSContDestroy(item->contp);
  item->deleted = 1;

  return 0;
}

static int
ts_lua_flush_handler(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  ts_lua_http_intercept_item *item = TSContDataGet(contp);

  ts_lua_flush_cleanup(item);

  TSContCall(item->ictx->contp, event, 0);

  return 0;
}
