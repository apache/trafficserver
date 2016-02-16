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

BindingInstance::BindingInstance() : lua(NULL)
{
}

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
  return (ptr == this->attachments.end()) ? NULL : ptr->second;
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
  const char *end = name;
  bool bound = false;

  int depth = 0;

  // Make the value an absolute stack inde because we are going to
  // invalidate relative indices.
  value = lua_absolute_index(this->lua, value);

  // XXX extract this code so that we can using it for binding constants
  // into an arbitrary table path ...
  Debug("lua", "binding %s value at %d to %s\n", luaL_typename(this->lua, value), value, name);

  for (; (end = ::strchr(start, '.')); start = end + 1) {
    std::string name(start, end);

    Debug("lua", "checking for table '%s'\n", name.c_str());
    if (depth == 0) {
      lua_getglobal(this->lua, name.c_str());
      if (lua_isnil(this->lua, -1)) {
        // No table with this name, construct one.
        Debug("lua", "creating global table '%s'\n", name.c_str());

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

      Debug("lua", "checking for table key '%s'\n", name.c_str());

      // Push the string key.
      lua_pushlstring(this->lua, &name[0], name.size());
      // Get the table entry (now on top of the stack).
      lua_gettable(this->lua, -2);

      if (lua_isnil(this->lua, -1)) {
        Debug("lua", "creating table key '%s'\n", name.c_str());

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

  Debug("lua", "stack depth is %d (expected %d)\n", lua_gettop(this->lua), depth);
  Debug("lua", "last name token is '%s'\n", start);

  // If we pushed a series of tables onto the stack, bind the name to a table
  // entry. otherwise bind it as a global name.
  if (depth) {
    bool isnil;

    // At this point the top of stack should be something indexable.
    ink_assert(is_indexable(this->lua, -1));

    Debug("lua", "stack depth is %d (expected %d)\n", lua_gettop(this->lua), depth);
    // Push the index name.
    lua_pushstring(this->lua, start);

    Debug("lua", "stack depth is %d (expected %d)\n", lua_gettop(this->lua), depth);
    // Fetch the index (without metamethods);
    lua_gettable(this->lua, -2);

    // Only push the value if it is currently nil.
    isnil = lua_isnil(this->lua, -1);
    lua_pop(this->lua, 1);
    Debug("lua", "isnil? %s", isnil ? "yes" : "no");

    if (isnil) {
      lua_pushstring(this->lua, start);
      lua_pushvalue(this->lua, value);
      lua_settable(this->lua, -3);
      bound = true;
    }

    Debug("lua", "stack depth is %d (expected %d)\n", lua_gettop(this->lua), depth);
    lua_pop(this->lua, depth);
  } else {
    bool isnil;

    lua_getglobal(this->lua, start);
    isnil = lua_isnil(this->lua, -1);
    lua_pop(this->lua, 1);

    if (isnil) {
      lua_pushvalue(this->lua, value);
      lua_setglobal(this->lua, start);
      bound = true;
    }
  }

  return bound;
}

bool
BindingInstance::construct()
{
  ink_release_assert(this->lua == NULL);

  if ((this->lua = luaL_newstate())) {
    luaL_openlibs(this->lua);

    // Push a pointer to ourself into the well-known registry key.
    lua_pushlightuserdata(this->lua, this);
    lua_setfield(this->lua, LUA_REGISTRYINDEX, selfkey);

    ink_release_assert(BindingInstance::self(this->lua) == this);
  }

  return this->lua;
}

bool
BindingInstance::require(const char *path)
{
  ink_release_assert(this->lua != NULL);

  if (luaL_dofile(this->lua, path) != 0) {
    Warning("%s", lua_tostring(this->lua, -1));
    lua_pop(this->lua, 1);
    return false;
  }

  return true;
}

BindingInstance *
BindingInstance::self(lua_State *lua)
{
  BindingInstance *binding;

  lua_getfield(lua, LUA_REGISTRYINDEX, selfkey);
  binding = (BindingInstance *)lua_touserdata(lua, -1);

  ink_release_assert(binding != NULL);
  ink_release_assert(binding->lua == lua);

  lua_pop(lua, 1);
  return binding;
}

void
BindingInstance::typecheck(lua_State *lua, const char *name, ...)
{
  int nargs = lua_gettop(lua);
  int seen = 0;
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
  luaL_register(lua, NULL, metatable);

  lua_pop(lua, 1); /* drop metatable */

  ink_assert(lua_gettop(lua) == 0);
}
