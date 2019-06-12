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

static TSTextLogObject log;

static int ts_lua_log_object_creat(lua_State *L);
static int ts_lua_log_object_write(lua_State *L);
static int ts_lua_log_object_destroy(lua_State *L);

static void ts_lua_inject_log_object_creat_api(lua_State *L);
static void ts_lua_inject_log_object_write_api(lua_State *L);
static void ts_lua_inject_log_object_destroy_api(lua_State *L);

void
ts_lua_inject_log_api(lua_State *L)
{
  lua_newtable(L);

  ts_lua_inject_log_object_creat_api(L);
  ts_lua_inject_log_object_write_api(L);
  ts_lua_inject_log_object_destroy_api(L);

  lua_setfield(L, -2, "log");
}

static void
ts_lua_inject_log_object_creat_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_log_object_creat);
  lua_setfield(L, -2, "object_creat");
}

static int
ts_lua_log_object_creat(lua_State *L)
{
  const char *log_name;
  size_t name_len;
  int log_mode;
  TSReturnCode error;

  log_name = luaL_checklstring(L, -2, &name_len);

  if (lua_isnil(L, 3)) {
    TSError("[ts_lua] No log name!!");
    return -1;
  } else {
    log_mode = luaL_checknumber(L, 3);
  }

  error = TSTextLogObjectCreate(log_name, log_mode, &log);

  if (!log || error == TS_ERROR) {
    TSError("[ts_lua] Unable to create log <%s>", log_name);
    return -1;
  }
  return 0;
}

static void
ts_lua_inject_log_object_write_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_log_object_write);
  lua_setfield(L, -2, "object_write");
}

static int
ts_lua_log_object_write(lua_State *L)
{
  const char *text;
  size_t text_len;

  text = luaL_checklstring(L, 1, &text_len);
  if (log) {
    TSTextLogObjectWrite(log, (char *)text, NULL);
  } else {
    TSError("[ts_lua][%s] log object does not exist for write", __FUNCTION__);
  }

  return 0;
}

static void
ts_lua_inject_log_object_destroy_api(lua_State *L)
{
  lua_pushcfunction(L, ts_lua_log_object_destroy);
  lua_setfield(L, -2, "object_destroy");
}

static int
ts_lua_log_object_destroy(lua_State *L ATS_UNUSED)
{
  if (TSTextLogObjectDestroy(log) != TS_SUCCESS) {
    TSError("[ts_lua][%s] TSTextLogObjectDestroy error!", __FUNCTION__);
  }

  return 0;
}
