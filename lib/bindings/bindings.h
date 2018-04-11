/** @file
 *
 *  Lua bindings object.
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

#pragma once

#include <string>
#include <map>
#include "lua.h"

struct BindingInstance {
  BindingInstance();
  ~BindingInstance();

  // Construct this Lua bindings instance.
  bool construct();

  // Import a Lua file.
  bool require(const char *path);

  // Eval a chunk of Lua code.
  bool eval(const char *chunk);

  // Bind values to the specified global name. If the name contains '.'
  // separators, intermediate tables are constucted and the value is bound
  // to the final path component.
  bool bind_constant(const char *name, lua_Integer value);
  bool bind_constant(const char *name, const char *value);
  bool bind_function(const char *name, int (*value)(lua_State *));
  bool bind_value(const char *name, int value);

  // Attach a named pointer that we can later fish out from a Lua state.
  void attach_ptr(const char *, void *);
  void *retrieve_ptr(const char *);

  // Generic typecheck helper for Lua APIs. Pass in a list of Lua type IDs
  // (ie. LUA_Txxx) terminated by LUA_TNONE. Throws a Lua error string on
  // failure.
  static void typecheck(lua_State *, const char *name, ...);

  // Given a Lua state, return the binding instance that owns it.
  static BindingInstance *self(lua_State *);

  // Register a Lua metatable for a custom type.
  static void register_metatable(lua_State *, const char *, const luaL_reg *);

  lua_State *lua;

  BindingInstance(const BindingInstance &) = delete;
  BindingInstance &operator=(const BindingInstance &) = delete;

private:
  std::map<std::string, void *> attachments;
};
