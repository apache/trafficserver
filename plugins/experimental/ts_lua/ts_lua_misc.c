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
}

static int
ts_lua_get_now_time(lua_State *L)
{
    time_t    now;

    now = TShrtime() / 1000000000;
    lua_pushnumber(L, now);
    return 1;
}

static int
ts_lua_debug(lua_State *L)
{
    const char      *msg;

    msg = luaL_checkstring(L, 1);
    TSDebug(TS_LUA_DEBUG_TAG, "%s", msg);
    return 0;
}

static int
ts_lua_error(lua_State *L)
{
    const char      *msg;

    msg = luaL_checkstring(L, 1);
    TSError("%s", msg);
    return 0;
}

static int
ts_lua_sleep(lua_State *L)
{
    int     sec;
    ts_lua_http_intercept_ctx *ictx;

    ictx = ts_lua_get_http_intercept_ctx(L);
    sec = luaL_checknumber(L, 1);

    TSContSchedule(ictx->contp, sec*1000, TS_THREAD_POOL_DEFAULT);
    return lua_yield(L, 0);
}

