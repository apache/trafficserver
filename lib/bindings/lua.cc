/** @file
 *
 *  Lua utilities and extensions.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "lua.h"

int
lua_absolute_index(lua_State *L, int relative)
{
  return (relative > 0 || relative <= LUA_REGISTRYINDEX) ? relative : lua_gettop(L) + relative + 1;
}

// Check the type at the given index. Error if it is not the expected type.
void
lua_checktype(lua_State *L, int index, int ltype)
{
  if (lua_type(L, index) != ltype) {
    luaL_error(L, "bad type, expected '%s' but found '%s'", lua_typename(L, ltype), lua_typename(L, lua_type(L, index)));
  }
}

// luaL_checkudata() throws an exception if it fails, so to accept variadic
// user types, we need to non-destructively test whether a userdata is an
// instance of the type we want.
bool
lua_is_userdata(lua_State *L, int index, const char *metatype)
{
  int target = lua_absolute_index(L, index);
  bool result = false;

  // Get the metatable of the target.
  if (lua_getmetatable(L, target) != 0) {
    // If there was one, get the metatable of the target type.
    luaL_getmetatable(L, metatype);

    // Compare them.
    result = lua_equal(L, -1, -2) == 1;

    // Pop the 2 metatables.
    lua_pop(L, 2);
  }

  return result;
}

template <> lua_Integer
lua_getfield(lua_State *L, int table, const char *key, lua_Integer default_value)
{
  lua_Integer result = default_value;

  lua_pushvalue(L, table); // Table is at -1.
  lua_pushstring(L, key); // Now key is at -1 and table is at -2.
  lua_gettable(L, -2); // Now the result is at -1.

  if (!lua_isnil(L, -1)) {
    result = lua_tointeger(L, -1);
  }

  lua_pop(L, 2); // Pop the result and the table.

  return result;
}

template <> const char *
lua_getfield(lua_State *L, int table, const char *key, const char * default_value)
{
  const char * result = default_value;

  lua_pushvalue(L, table); // Table is at -1.
  lua_pushstring(L, key); // Now key is at -1 and table is at -2.
  lua_gettable(L, -2); // Now the result is at -1.

  if (!lua_isnil(L, -1)) {
    result = lua_tostring(L, -1);
  }

  lua_pop(L, 2); // Pop the result and the table.
  return result;
}
