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

#include <ts/ts.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

void *
LuaAllocate(void * ud, void * ptr, size_t osize, size_t nsize)
{
  TSReleaseAssert(ud == NULL);

  if (nsize == 0) {
    TSfree(ptr);
    return NULL;
  }

  return TSrealloc(ptr, nsize);
}

void
LuaPushMetatable(lua_State * lua, const char * name, const luaL_Reg * exports)
{
  luaL_newmetatable(lua, name);
  lua_pushvalue(lua, -1);
  lua_setfield(lua, -2, "__index");
  luaL_register(lua, NULL, exports);
}

void
LuaRegisterLibrary(lua_State * lua, const char * name, lua_CFunction loader)
{
  // Pull up the preload table.
  lua_getglobal(lua, "package");
  lua_getfield(lua, -1, "preload");

  lua_pushcfunction(lua, loader);
  lua_setfield(lua, -2, name);

  // Pop the 'package' and 'preload' tables.
  lua_pop(lua, 2);
}

void
LuaLoadLibraries(lua_State * lua)
{
#define REGISTER_LIBRARY(name) LuaRegisterLibrary(lua, #name, luaopen_ ## name)

    lua_cpcall(lua, luaopen_base, NULL);
    lua_cpcall(lua, luaopen_package, NULL);

    REGISTER_LIBRARY(io);
    REGISTER_LIBRARY(os);
    REGISTER_LIBRARY(table);
    REGISTER_LIBRARY(string);
    REGISTER_LIBRARY(math);
    REGISTER_LIBRARY(debug);

#undef REGISTER_LIBRARY
}

