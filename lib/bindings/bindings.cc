/** @file
 *
 *  A brief file description
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

#include "bindings.h"
#include "ts/Diags.h"

static const char selfkey[] = "bb3ecc8d-de6b-4f48-9aca-b3a3f14bdbad";

static bool
is_indexable(lua_State *L, int index)
{
  return lua_istable(L, index) || lua_isuserdata(L, index);
}

BindingInstance::BindingInstance() : lua(nullptr) {}

BindingInstance::~BindingInstance()
{
  if (this->lua) {
    lua_close(this->lua);
  }
}

void
BindingInstance::attach_ptr(const char *name, void *ptr)
{
  this->attachments[name] = ptr;
}

void *
BindingInstance::retrieve_ptr(const char *name)
{
  auto ptr = this->attachments.find(name);
  return (ptr == this->attachments.end()) ? nullptr : ptr->second;
}

bool
BindingInstance::bind_constant(const char *name, lua_Integer value)
{
  bool bound;

  lua_pushinteger(this->lua, value);
  bound = this->bind_value(name, -1);
  lua_pop(this->lua, 1);

  return bound;
}

bool
BindingInstance::bind_constant(const char *name, const char *value)
{
  bool bound;

  lua_pushlstring(this->lua, value, strlen(value));
  bound = this->bind_value(name, -1);
  lua_pop(this->lua, 1);

  return bound;
}

bool
BindingInstance::bind_function(const char *name, int (*value)(lua_State *))
{
  bool bound;

  lua_pushcfunction(this->lua, value);
  bound = this->bind_value(name, -1);
  lua_pop(this->lua, 1);

  return bound;
}

// Bind an arbitrary Lua value from the give stack position.
bool
BindingInstance::bind_value(const char *name, int value)
{
  const char *start = name;
  const char *end   = name;

  int depth = 0;

  // Make the value an absolute stack inde because we are going to
  // invalidate relative indices.
  value = lua_absolute_index(this->lua, value);

  // XXX extract this code so that we can using it for binding constants
  // into an arbitrary table path ...
  Debug("lua", "binding %s value at %d to %s", luaL_typename(this->lua, value), value, name);

  for (; (end = ::strchr(start, '.')); start = end + 1) {
    std::string name(start, end);

    Debug("lua", "checking for table '%s'", name.c_str());
    if (depth == 0) {
      lua_getglobal(this->lua, name.c_str());
      if (lua_isnil(this->lua, -1)) {
        // No table with this name, construct one.
        Debug("lua", "creating global table '%s'", name.c_str());

        lua_pop(this->lua, 1); // Pop the nil.
        lua_newtable(this->lua);
        lua_setglobal(this->lua, name.c_str());
        lua_getglobal(this->lua, name.c_str());

        // Top of stack MUST be a table now.
        ink_assert(lua_istable(this->lua, -1));
      }

      ink_assert(is_indexable(this->lua, -1));
    } else {
      ink_assert(is_indexable(this->lua, -1));

      Debug("lua", "checking for table key '%s'", name.c_str());

      // Push the string key.
      lua_pushlstring(this->lua, &name[0], name.size());
      // Get the table entry (now on top of the stack).
      lua_gettable(this->lua, -2);

      if (lua_isnil(this->lua, -1)) {
        Debug("lua", "creating table key '%s'", name.c_str());

        lua_pop(this->lua, 1); // Pop the nil.
        lua_pushlstring(this->lua, &name[0], name.size());
        lua_newtable(this->lua);

        // Set the table entry. The stack now looks like:
        //  -1  value (the new table)
        //  -2  index (string)
        //  -3  target (the table to add the index to)
        lua_settable(this->lua, -3);

        // Get the table entry we just created.
        lua_pushlstring(this->lua, &name[0], name.size());
        lua_gettable(this->lua, -2);

        // Top of stack MUST be a table now.
        ink_assert(lua_istable(this->lua, -1));
      }

      // The new entry is on top of the stack.
      ink_assert(is_indexable(this->lua, -1));
    }

    ++depth;
  }

  Debug("lua", "stack depth is %d (expected %d)", lua_gettop(this->lua), depth);
  Debug("lua", "last name token is '%s'", start);

  // If we pushed a series of tables onto the stack, bind the name to a table
  // entry. otherwise bind it as a global name.
  if (depth) {
    // At this point the top of stack should be something indexable.
    ink_assert(is_indexable(this->lua, -1));

    Debug("lua", "stack depth is %d (expected %d)", lua_gettop(this->lua), depth);

    lua_pushstring(this->lua, start);
    lua_pushvalue(this->lua, value);
    lua_settable(this->lua, -3);

    Debug("lua", "stack depth is %d (expected %d)", lua_gettop(this->lua), depth);
    lua_pop(this->lua, depth);
  } else {
    // Always push the value so we can get the update
    lua_pushvalue(this->lua, value);
    lua_setglobal(this->lua, start);
  }

  return true;
}

bool
BindingInstance::construct()
{
  ink_release_assert(this->lua == nullptr);

  if ((this->lua = luaL_newstate())) {
    luaL_openlibs(this->lua);

    // Push a pointer to ourself into the well-known registry key.

    // We do not use lightuserdata here because BindingInstance variables
    // are often declared on stack which would make "this" a stack variable.
    // While this might seem fine and actually work on many platforms, those
    // 64bit platforms with split VA space where heap and stack may live in
    // a separate 47bit VA will violate internal assumptions that luajit
    // places on lightuserdata. Plain userdata will provide luajit-happy
    // address in which we have the full 64bits to store our pointer to this.
    // see: https://www.circonus.com/2016/07/luajit-illumos-vm/

    BindingInstance **lua_surrogate;
    lua_surrogate  = (BindingInstance **)lua_newuserdata(this->lua, sizeof(BindingInstance *));
    *lua_surrogate = this;
    lua_setfield(this->lua, LUA_REGISTRYINDEX, selfkey);

    ink_release_assert(BindingInstance::self(this->lua) == this);
  }

  return this->lua;
}

bool
BindingInstance::require(const char *path)
{
  ink_release_assert(this->lua != nullptr);

  if (luaL_dofile(this->lua, path) != 0) {
    Warning("%s", lua_tostring(this->lua, -1));
    lua_pop(this->lua, 1);
    return false;
  }

  return true;
}

bool
BindingInstance::eval(const char *chunk)
{
  ink_release_assert(this->lua != nullptr);

  if (luaL_dostring(this->lua, chunk) != 0) {
    const char *w = lua_tostring(this->lua, -1);
    Warning("%s", w);
    lua_pop(this->lua, 1);
    return false;
  }

  return true;
}

BindingInstance *
BindingInstance::self(lua_State *lua)
{
  BindingInstance **binding;

  lua_getfield(lua, LUA_REGISTRYINDEX, selfkey);
  binding = (BindingInstance **)lua_touserdata(lua, -1);

  ink_release_assert(binding != nullptr);
  ink_release_assert(*binding != nullptr);
  ink_release_assert((*binding)->lua == lua);

  lua_pop(lua, 1);
  return *binding;
}

void
BindingInstance::typecheck(lua_State *lua, const char *name, ...)
{
  int nargs = lua_gettop(lua);
  int seen  = 0;
  va_list ap;

  va_start(ap, name);

  for (; seen < nargs; ++seen) {
    int expected = va_arg(ap, int);

    if (expected == LUA_TNONE) {
      va_end(ap);
      luaL_error(lua, "too many arguments to '%s'", name);
      return;
    }

    if (lua_type(lua, seen + 1) != expected) {
      va_end(ap);
      luaL_error(lua, "bad argument #%d to '%s' (expected %s, received %s)", seen + 1, name, lua_typename(lua, expected),
                 lua_typename(lua, lua_type(lua, seen + 1)));
      return;
    }
  }

  va_end(ap);

  if (seen != nargs) {
    luaL_error(lua, "too few arguments to '%s' (seen %d, nargs %d)", name, seen, nargs);
  }
}

void
BindingInstance::register_metatable(lua_State *lua, const char *name, const luaL_reg *metatable)
{
  // Create a metatable, adding it to the Lua registry.
  luaL_newmetatable(lua, name);
  // Dup the metatable.
  lua_pushvalue(lua, -1);
  // Pop one of those copies and assign it to __index field on the 1st metatable
  lua_setfield(lua, -2, "__index");
  // register functions in the metatable
  luaL_register(lua, nullptr, metatable);

  lua_pop(lua, 1); /* drop metatable */

  ink_assert(lua_gettop(lua) == 0);
}
