/** @file

  Lua bindings for librecords.

  @section license License

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

#include "bindings.h"
#include "metrics.h"
#include "P_RecCore.h"
#include "ts/ink_memory.h"
#include <map>
#include <set>

#define BINDING "lua.metrics"

struct metrics_binding {
  static metrics_binding *check(lua_State *L, int index);

  typedef std::map<std::string, int> ref_map;

  ats_scoped_str prefix;
  size_t prefixlen;
  ref_map refs;
};

metrics_binding *
metrics_binding::check(lua_State *L, int index)
{
  metrics_binding *m;

  luaL_checktype(L, index, LUA_TUSERDATA);
  m = (metrics_binding *)luaL_checkudata(L, index, BINDING);
  if (m == NULL) {
    luaL_typerror(L, index, "userdata");
  }

  return m;
}

static bool
metrics_record_exists(const char *name)
{
  RecT rec_type;
  return RecGetRecordType(name, &rec_type) == REC_ERR_OKAY;
}

// Push the value of a record onto the Lua stack.
static void
metrics_push_record(const RecRecord *rec, void *ptr)
{
  lua_State *L = (lua_State *)ptr;

  ink_assert(REC_TYPE_IS_STAT(rec->rec_type));

  switch (rec->data_type) {
  case RECD_INT: /* fallthru */
  case RECD_COUNTER:
    lua_pushinteger(L, rec->data.rec_int);
    break;
  case RECD_FLOAT:
    lua_pushnumber(L, rec->data.rec_float);
    break;
  case RECD_STRING:
    lua_pushlstring(L, rec->data.rec_string, strlen(rec->data.rec_string));
    break;
  default:
    lua_pushnil(L);
  }
}

// Return the value of a metric, relative to the bound prefix.
static int
metrics_index(lua_State *L)
{
  metrics_binding *m = metrics_binding::check(L, 1);
  metrics_binding::ref_map::iterator ptr;

  const char *key;
  size_t len;

  key = luaL_checklstring(L, 2, &len);
  ink_release_assert(key != NULL && len != 0);

  // First, check whether we have a reference stored for this key.
  ptr = m->refs.find(std::string(key, len));
  if (ptr != m->refs.end()) {
    // We have a ref, so push the saved table reference to the stack.
    lua_rawgeti(L, LUA_REGISTRYINDEX, ptr->second);
  } else {
    char name[m->prefixlen + sizeof(".") + len];

    snprintf(name, sizeof(name), "%s.%.*s", m->prefix.get(), (int)len, key);

    // Push the indexed record value, or nil if there is nothing there.
    if (RecLookupRecord(name, metrics_push_record, L) != REC_ERR_OKAY) {
      lua_pushnil(L);
    }
  }

  return 1;
}

static int
metrics_newindex(lua_State *L)
{
  // The stack now looks like:
  //  1   the table value (userdata)
  //  2   key to index (string)
  //  3   value to insert (should be a table)

  metrics_binding *m = metrics_binding::check(L, 1);
  const char *key;
  size_t len;
  metrics_binding::ref_map::iterator ptr;

  key = luaL_checklstring(L, 2, &len);
  switch (lua_type(L, 3)) {
  case LUA_TUSERDATA:
    metrics_binding::check(L, 3);
    break;
  case LUA_TTABLE:
    break;
  default:
    luaL_typerror(L, 3, "userdata or table");
  }

  char name[m->prefixlen + sizeof(".") + len];

  snprintf(name, sizeof(name), "%s.%.*s", m->prefix.get(), (int)len, key);

  // If this index is already a record, don't overwrite it.
  if (metrics_record_exists(name)) {
    return 0;
  }

  ptr = m->refs.find(std::string(key, len));
  if (ptr != m->refs.end()) {
    // Remove the previously saved reference.
    luaL_unref(L, LUA_REGISTRYINDEX, ptr->second);
  }

  // Pop the top of the stack into a reference that we store in the refmap.
  lua_pushvalue(L, 3);
  m->refs[std::string(key, len)] = luaL_ref(L, LUA_REGISTRYINDEX);

  return 0;
}

static int
metrics_gc(lua_State *L)
{
  metrics_binding *m = metrics_binding::check(L, 1);

  // Clean up any references we stashed.
  for (metrics_binding::ref_map::iterator ptr = m->refs.begin(); ptr != m->refs.end(); ++ptr) {
    luaL_unref(L, LUA_REGISTRYINDEX, ptr->second);
  }

  m->~metrics_binding();
  return 0;
}

int
lua_metrics_new(const char *prefix, lua_State *L)
{
  metrics_binding *m = lua_newuserobject<metrics_binding>(L);

  Debug("lua", "new metrics binding for prefix %s", prefix);
  m->prefix = ats_strdup(prefix);
  m->prefixlen = strlen(prefix);

  luaL_getmetatable(L, BINDING);
  lua_setmetatable(L, -2);

  // Leave the userdata on the stack.
  return 1;
}

void
lua_metrics_register(lua_State *L)
{
  static const luaL_reg metatable[] = {{"__gc", metrics_gc}, {"__index", metrics_index}, {"__newindex", metrics_newindex}, {0, 0}};

  BindingInstance::register_metatable(L, BINDING, metatable);
}

static void
install_metrics_object(RecT rec_type, void *edata, int registered, const char *name, int data_type, RecData *datum)
{
  std::set<std::string> *prefixes = (std::set<std::string> *)edata;

  if (likely(registered)) {
    const char *end = strrchr(name, '.');
    ptrdiff_t len = end - name;
    prefixes->insert(std::string(name, len));
  }
}

int
lua_metrics_install(lua_State *L)
{
  int count = 0;
  int metrics_type = RECT_NODE | RECT_PROCESS | RECT_CLUSTER | RECT_PLUGIN;
  BindingInstance *binding = BindingInstance::self(L);
  std::set<std::string> prefixes;

  // Gather all the metrics namespace prefixes into a sorted set. We want to install
  // metrics objects as the last branch of the namespace so that leaf metrics lookup
  // end up indexing metrics objects.
  RecDumpRecords((RecT)metrics_type, install_metrics_object, &prefixes);

  for (std::set<std::string>::const_iterator p = prefixes.cbegin(); p != prefixes.cend(); ++p) {
    if (lua_metrics_new(p->c_str(), binding->lua) == 1) {
      if (binding->bind_value(p->c_str(), -1)) {
        Debug("lua", "installed metrics object at prefix %s", p->c_str());
        ++count;
      }

      lua_pop(binding->lua, 1);
    }
  }

  // Return the number of metrics we installed;
  lua_pushinteger(L, count);
  return 1;
}
