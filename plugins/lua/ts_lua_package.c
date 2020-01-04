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

#define TS_LUA_MAX_PACKAGE_PATH_LEN 256
#define TS_LUA_MAX_PACKAGE_NUM 64

typedef struct {
  size_t len;
  char *name;
} ts_lua_package_path;

static int path_cnt = 0;
static ts_lua_package_path path[TS_LUA_MAX_PACKAGE_NUM];

static int cpath_cnt = 0;
static ts_lua_package_path cpath[TS_LUA_MAX_PACKAGE_NUM];

static int g_path_cnt = 0;
static ts_lua_package_path g_path[TS_LUA_MAX_PACKAGE_NUM];

static int g_cpath_cnt = 0;
static ts_lua_package_path g_cpath[TS_LUA_MAX_PACKAGE_NUM];

static int ts_lua_add_package_path(lua_State *L);
static int ts_lua_add_package_cpath(lua_State *L);
static int ts_lua_add_package_path_items(lua_State *L, ts_lua_package_path *pp, int n);
static int ts_lua_add_package_cpath_items(lua_State *L, ts_lua_package_path *pp, int n);

void
ts_lua_inject_package_api(lua_State *L)
{
  /* ts.add_package_path() */
  lua_pushcfunction(L, ts_lua_add_package_path);
  lua_setfield(L, -2, "add_package_path");

  /* ts.add_package_cpath(...) */
  lua_pushcfunction(L, ts_lua_add_package_cpath);
  lua_setfield(L, -2, "add_package_cpath");
}

static int
ts_lua_add_package_path(lua_State *L)
{
  ts_lua_instance_conf *conf;
  const char *data;
  const char *ptr, *end, *hit;
  size_t dlen;
  int i, n;
  size_t item_len;
  ts_lua_package_path pp[TS_LUA_MAX_PACKAGE_NUM];
  ts_lua_package_path *elt;

  conf = ts_lua_get_instance_conf(L);
  if (conf == NULL) {
    return luaL_error(L, "can't get the instance conf");
  }

  data = luaL_checklstring(L, 1, &dlen);
  end  = data + dlen;

  ptr = data;
  n   = 0;

  while (ptr < end) {
    hit = memchr(ptr, ';', end - ptr);

    if (hit) {
      item_len = hit - ptr;

    } else {
      item_len = end - ptr;
    }

    if (item_len > 0) {
      if (!conf->remap) {
        for (i = 0; i < g_path_cnt; i++) {
          if (g_path[i].len == item_len && memcmp(g_path[i].name, ptr, item_len) == 0) // exist
          {
            break;
          }
        }

        if (i >= g_path_cnt) {
          if (n + i >= TS_LUA_MAX_PACKAGE_NUM)
            return luaL_error(L, "extended package path number exceeds %d", TS_LUA_MAX_PACKAGE_NUM);

          pp[n].name = (char *)ptr;
          pp[n].len  = item_len;
          n++;
        }
      } else {
        for (i = 0; i < path_cnt; i++) {
          if (path[i].len == item_len && memcmp(path[i].name, ptr, item_len) == 0) // exist
          {
            break;
          }
        }

        if (i >= path_cnt) {
          if (n + i >= TS_LUA_MAX_PACKAGE_NUM)
            return luaL_error(L, "extended package path number exceeds %d", TS_LUA_MAX_PACKAGE_NUM);

          pp[n].name = (char *)ptr;
          pp[n].len  = item_len;
          n++;
        }
      }
    }

    ptr += item_len + 1; // ??
  }

  if (n > 0) {
    ts_lua_add_package_path_items(L, pp, n);

    if (conf->_last) {
      if (!conf->remap) {
        elt = &g_path[g_path_cnt];
      } else {
        elt = &path[path_cnt];
      }

      for (i = 0; i < n; i++) {
        elt->len  = pp[i].len;
        elt->name = (char *)TSmalloc(pp[i].len);
        memcpy(elt->name, pp[i].name, pp[i].len);
        elt++;
      }

      if (!conf->remap) {
        g_path_cnt += n;
      } else {
        path_cnt += n;
      }
    }
  }

  return 0;
}

static int
ts_lua_add_package_path_items(lua_State *L, ts_lua_package_path *pp, int n)
{
  int i, base;
  const char *old_path;
  char new_path[2048];
  size_t old_path_len, new_path_len;

  base = lua_gettop(L);

  lua_getglobal(L, "package");

  if (!lua_istable(L, -1)) {
    return luaL_error(L, "'package' table does not exist");
  }

  lua_getfield(L, -1, "path"); /* get old package.path */

  old_path = lua_tolstring(L, -1, &old_path_len);
  if (old_path[old_path_len - 1] == ';') {
    old_path_len--;
  }

  new_path_len = snprintf(new_path, sizeof(new_path) - 32, "%.*s", (int)old_path_len, old_path);

  for (i = 0; i < n; i++) {
    if (new_path_len + pp[i].len + 1 >= sizeof(new_path)) {
      TSError("[ts_lua][%s] Extended package.path is too long", __FUNCTION__);
      return -1;
    }

    new_path[new_path_len++] = ';';
    memcpy(new_path + new_path_len, pp[i].name, pp[i].len);
    new_path_len += pp[i].len;
  }

  new_path[new_path_len] = 0;

  lua_pushlstring(L, new_path, new_path_len);
  lua_setfield(L, -3, "path");

  lua_settop(L, base);

  return 0;
}

static int
ts_lua_add_package_cpath(lua_State *L)
{
  ts_lua_instance_conf *conf;
  const char *data;
  const char *ptr, *end, *hit;
  size_t dlen;
  int i, n;
  size_t item_len;
  ts_lua_package_path pp[TS_LUA_MAX_PACKAGE_NUM];
  ts_lua_package_path *elt;

  conf = ts_lua_get_instance_conf(L);
  if (conf == NULL) {
    return luaL_error(L, "can't get the instance conf");
  }

  data = luaL_checklstring(L, 1, &dlen);
  end  = data + dlen;

  ptr = data;
  n   = 0;

  while (ptr < end) {
    hit = memchr(ptr, ';', end - ptr);

    if (hit) {
      item_len = hit - ptr;

    } else {
      item_len = end - ptr;
    }

    if (item_len > 0) {
      if (!conf->remap) {
        for (i = 0; i < g_cpath_cnt; i++) {
          if (g_cpath[i].len == item_len && memcmp(g_cpath[i].name, ptr, item_len) == 0) // exist
          {
            break;
          }
        }

        if (i >= g_cpath_cnt) {
          if (n + i >= TS_LUA_MAX_PACKAGE_NUM)
            return luaL_error(L, "extended package cpath number exceeds %d", TS_LUA_MAX_PACKAGE_NUM);

          pp[n].name = (char *)ptr;
          pp[n].len  = item_len;
          n++;
        }
      } else {
        for (i = 0; i < cpath_cnt; i++) {
          if (cpath[i].len == item_len && memcmp(cpath[i].name, ptr, item_len) == 0) // exist
          {
            break;
          }
        }

        if (i >= cpath_cnt) {
          if (n + i >= TS_LUA_MAX_PACKAGE_NUM)
            return luaL_error(L, "extended package cpath number exceeds %d", TS_LUA_MAX_PACKAGE_NUM);

          pp[n].name = (char *)ptr;
          pp[n].len  = item_len;
          n++;
        }
      }
    }

    ptr += item_len + 1; // ??
  }

  if (n > 0) {
    ts_lua_add_package_cpath_items(L, pp, n);

    if (conf->_last) {
      if (!conf->remap) {
        elt = &g_cpath[g_cpath_cnt];
      } else {
        elt = &cpath[cpath_cnt];
      }

      for (i = 0; i < n; i++) {
        elt->len  = pp[i].len;
        elt->name = (char *)TSmalloc(pp[i].len);
        memcpy(elt->name, pp[i].name, pp[i].len);
        elt++;
      }

      if (!conf->remap) {
        g_cpath_cnt += n;
      } else {
        cpath_cnt += n;
      }
    }
  }

  return 0;
}

static int
ts_lua_add_package_cpath_items(lua_State *L, ts_lua_package_path *pp, int n)
{
  int i, base;
  const char *old_path;
  char new_path[2048];
  size_t old_path_len, new_path_len;

  base = lua_gettop(L);

  lua_getglobal(L, "package");

  if (!lua_istable(L, -1)) {
    return luaL_error(L, "'package' table does not exist");
  }

  lua_getfield(L, -1, "cpath"); /* get old package.cpath */

  old_path = lua_tolstring(L, -1, &old_path_len);
  if (old_path[old_path_len - 1] == ';') {
    old_path_len--;
  }

  new_path_len = snprintf(new_path, sizeof(new_path) - 32, "%.*s", (int)old_path_len, old_path);

  for (i = 0; i < n; i++) {
    if (new_path_len + pp[i].len + 1 >= sizeof(new_path)) {
      TSError("[ts_lua][%s] Extended package.cpath is too long", __FUNCTION__);
      return -1;
    }

    new_path[new_path_len++] = ';';
    memcpy(new_path + new_path_len, pp[i].name, pp[i].len);
    new_path_len += pp[i].len;
  }

  new_path[new_path_len] = 0;

  lua_pushlstring(L, new_path, new_path_len);
  lua_setfield(L, -3, "cpath");

  lua_settop(L, base);

  return 0;
}
