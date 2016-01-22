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

#ifndef LUA_H_7A9F5CCE_01C6_45C3_987A_FDCC1F437AA2
#define LUA_H_7A9F5CCE_01C6_45C3_987A_FDCC1F437AA2

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// Redeclare luaL_error with format string checking.
LUALIB_API int(luaL_error)(lua_State *L, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

// Our version of abs_index() from lauxlib.c. Converts a absolute or relative
// stack index into an absolute index. This is helpful for functions that
// accepts an index but are going to do stack manipulation themselves.
int lua_absolute_index(lua_State *L, int relative);

// Check the type at the given index. Error if it is not the expected type.
void lua_checktype(lua_State *L, int index, int ltype);

// luaL_checkudata() throws an exception if it fails, so to accept variadic
// user types, we need to non-destructively test whether a userdata is an
// instance of the type we want.
bool lua_is_userdata(lua_State *L, int index, const char *metatype);

// Like lua_newuserdata() but for C++ objects.
template <typename T>
T *
lua_newuserobject(lua_State *L)
{
  T *ptr = (T *)lua_newuserdata(L, sizeof(T));
  if (ptr) {
    return new (ptr) T();
  }

  return (T *)NULL;
}

#endif /* LUA_H_7A9F5CCE_01C6_45C3_987A_FDCC1F437AA2 */
