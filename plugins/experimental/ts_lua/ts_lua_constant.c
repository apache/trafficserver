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

#include <stdint.h>
#include <stdio.h>
#include <lua.h>

static void ts_lua_inject_number_variables(lua_State *L);

void
ts_lua_inject_constant_api(lua_State *L)
{
  ts_lua_inject_number_variables(L);
}

static void
ts_lua_inject_number_variables(lua_State *L)
{
  lua_pushinteger(L, INT64_MAX);
  lua_setglobal(L, "TS_LUA_INT64_MAX");

  lua_pushinteger(L, INT64_MIN);
  lua_setglobal(L, "TS_LUA_INT64_MIN");
}
