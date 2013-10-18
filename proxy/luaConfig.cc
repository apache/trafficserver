/** @file

  A brief file description

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

#include "libts.h"
#include "luaConfig.h"
#include "I_Layout.h"
#include "I_RecDefs.h"
#include "I_RecCore.h"
#include "P_RecCore.h"
#include "UrlRewrite.h"

luaConfig globalLuaConfig("tsconfig");

static int
tsrec_newindex_func(lua_State *L) {
  luaL_error(L, "cannot assign to ats.config_t, invoked instead");
  return 0;
}

static int
tsrec_index_func(lua_State *L) {
  const char *opath = NULL;
  char path[1024];
  assert(lua_gettop(L) == 2);
  if(!strcmp(lua_tostring(L,2),"_path")) return 0;
  lua_pushvalue(L,2);
  lua_rawget(L,1);
  if(lua_istable(L,-1)) return 1;
  lua_pop(L,1);

  lua_pushstring(L,"_path");
  lua_rawget(L,1);
  if(lua_isstring(L,-1)) opath = lua_tostring(L,-1);
  if(opath == NULL) snprintf(path, sizeof(path), "%s", lua_tostring(L,2));
  else snprintf(path, sizeof(path), "%s.%s", opath, lua_tostring(L,2));

  lua_newtable(L);
  lua_pushstring(L, path);
  lua_setfield(L, -2, "_path");

  luaL_getmetatable(L, "ats.config_t");
  lua_setmetatable(L, -2);

  lua_pushvalue(L,2); /* requested name */
  lua_pushvalue(L,-2);
  lua_rawset(L,1);
  return 1;
}

static int
tsrec_next_record(lua_State *L) {
  RecRecord *r = NULL;
  int *idx, i, num_records, upidx;
  upidx = lua_upvalueindex(1);
  idx = (int *)lua_touserdata(L,upidx);
  i = *idx;

  ink_mutex_acquire(&g_rec_config_lock);

  num_records = g_num_records;
  for (; i < num_records; i++) {
    r = &(g_records[i]);
    if (REC_TYPE_IS_CONFIG(r->rec_type))
      break;
    r = NULL;
  }
  if(r != NULL) rec_mutex_acquire(&(r->lock));
  ink_mutex_release(&g_rec_config_lock);
  *idx = i+1;

  if(r != NULL) {
    lua_newtable(L);
    lua_pushstring(L, r->name);
    lua_setfield(L, -2, "_path");
  
    luaL_getmetatable(L, "ats.config_t");
    lua_setmetatable(L, -2);
    rec_mutex_release(&(r->lock));
    return 1;
  }
  return 0;
}

static int
tsrec_dispatch_method(lua_State *L, const char *bpath, const char *method) {
  if(!strcmp(method, "name")) {
    lua_pushstring(L, bpath);
    return 1;
  }
  else if(!strcmp(method, "list")) {
    int *idx = (int *)lua_newuserdata(L, sizeof(int));
    *idx = 0;
    lua_pushcclosure(L, tsrec_next_record, 1);
    return 1;
  }
  luaL_error(L, "unknown method call: %s", method);
  return 0;
}

static int
tsrec_call_func(lua_State *L) {
  RecT rec_type;
  RecDataT data_type;
  const char *opath = NULL;
  int nargs = lua_gettop(L);
  lua_pushstring(L,"_path");
  lua_rawget(L,1);
  opath = lua_tostring(L,-1);
  lua_pop(L,1);

  if(lua_istable(L,2)) {
    const char *bpath;
    lua_pushstring(L,"_path");
    lua_rawget(L,2);
    bpath = lua_tostring(L,-1);
    if(bpath == NULL) return tsrec_dispatch_method(L,"",opath);
    int blen = strlen(bpath);
    lua_pop(L,1);
    if(blen > strlen(opath) ||
       strncmp(bpath, opath, blen) ||
       opath[blen] != '.') {
      luaL_error(L, "impossible method call: %s", opath + blen + 1);
    }
    return tsrec_dispatch_method(L,bpath,opath + blen + 1);
  }

  if(RecGetRecordType(opath, &rec_type))
    luaL_error(L, "Could not find record type '%s'", opath);
  if(RecGetRecordDataType(opath, &data_type))
    luaL_error(L, "Could not find record data type '%s'", opath);
  if(nargs == 1) {
    /* return the value */
    switch(data_type) {
      case RECD_INT:
        RecInt v_int;
        RecGetRecordInt(opath, &v_int);
        lua_pushinteger(L,v_int);
        return 1;
        break;
      case RECD_FLOAT:
        RecFloat v_float;
        RecGetRecordFloat(opath, &v_float);
        lua_pushnumber(L,v_float);
        return 1;
        break;
      case RECD_STRING:
        RecString v_string;
        RecGetRecordString_Xmalloc(opath, &v_string);
        lua_pushstring(L, v_string);
        ats_free(v_string);
        return 1;
        break;
      case RECD_COUNTER:
        RecCounter v_counter;
        RecGetRecordCounter(opath, &v_counter);
        lua_pushinteger(L,v_counter);
        return 1;
        break;
      default:
        luaL_error(L, "Unknown type of record: %s", opath);
        break;
    }
    return 0;
  }

  switch(data_type) {
    case RECD_INT:
      RecSetRecordInt(opath, (RecInt)luaL_checkint(L,2));
      break;
    case RECD_FLOAT:
      RecSetRecordFloat(opath, (RecFloat)luaL_checknumber(L,2));
      break;
    case RECD_STRING:
      RecSetRecordString(opath, (const RecString)lua_tostring(L,2));
      break;
    case RECD_COUNTER:
      RecSetRecordCounter(opath, (RecCounter)luaL_checklong(L,2));
      break;
    default:
      luaL_error(L, "Unknown type of record: %s", opath);
      break;
  }
  return 0;
}

static int
lua_ats_log(lua_State *L) {
  int type = lua_tointeger(L, lua_upvalueindex(1));
  switch(type) {
    case 0: Error("%s", lua_tostring(L,1)); break;
    case 1: Warning("%s", lua_tostring(L,1)); break;
    case 2: Note("%s", lua_tostring(L,1)); break;
    case 3: Status("%s", lua_tostring(L,1)); break;
    case 4: Emergency("%s", lua_tostring(L,1)); break;
    case 5: Fatal("%s", lua_tostring(L,1)); break;
    case 6: Debug(lua_tostring(L,1), "%s", lua_tostring(L,2)); break;
    default:
      luaL_error(L, "unknown internal log type");
      break;
  }
  return 0;
}
void luaopen_ats(lua_State *L) {
  luaL_newmetatable(L, "ats.config_t");
  lua_pushcclosure(L, tsrec_index_func, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcclosure(L, tsrec_newindex_func, 0);
  lua_setfield(L, -2, "__newindex");
  lua_pushcclosure(L, tsrec_call_func, 0);
  lua_setfield(L, -2, "__call");

  lua_newtable(L);
  lua_setglobal(L, "ats");
  lua_getglobal(L, "ats");

  /* config */
  lua_newtable(L);
  luaL_getmetatable(L, "ats.config_t");
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "config");
 
  lua_newtable(L); 
#define mkLog(id,name) do { \
  lua_pushinteger(L,id); \
  lua_pushcclosure(L, lua_ats_log, 1); \
  lua_setfield(L, -2, #name); \
} while(0)
  mkLog(0,error);
  mkLog(1,warning);
  mkLog(2,note);
  mkLog(3,status);
  mkLog(4,emergency);
  mkLog(5,fatal);
  mkLog(6,debug);
  lua_setfield(L, -2, "log");
}

void luaConfig::records() {
  int rv;
  lua_State *L = getL();
  lua_getglobal(L, "tsconfig");
  lua_getfield(L, -1, "config");
  if(lua_isnil(L,-1)) {
    lua_pop(L,1);
    return;
  }
  rv = lua_pcall(L, 0, 1, 0);
  if(rv != 0) {
    ink_error("tsconfig.config() failed: %s\n", lua_tostring(L,-1));
  }
  lua_pop(L,1);
}

void drop_lua_state_holder(void *vls) {
  struct luaConfigStateHolder *state_holder = (struct luaConfigStateHolder *)vls;
  lua_close(state_holder->states[0]);
  lua_close(state_holder->states[1]);
}

void luaConfig::boot() {
  assert(state_holder.states[0] == NULL);
  assert(state_holder.states[1] == NULL);
  char system_config_directory[PATH_NAME_MAX + 1]; // Layout->sysconfdir
  ink_strlcpy(system_config_directory, Layout::get()->sysconfdir, PATH_NAME_MAX);

  char config_file_path[PATH_NAME_MAX];
  config_file_path[0] = '\0';
  ink_strlcpy(config_file_path, system_config_directory, sizeof(config_file_path));
  ink_strlcat(config_file_path, "/?.lua", sizeof(config_file_path));
  setL(0, open(config_file_path, config_module));
  setL(1, open(config_file_path, config_module));
}

lua_State *luaConfig::open(const char *config_file_path, const char *module) {
  lua_State *L = luaL_newstate();
  if(L == NULL) {
    printf("[TrafficServer] failed to initialized lua.\n");
    exit(1);
  }
  luaL_openlibs(L);

  lua_getglobal(L, "package");
  lua_pushstring(L, config_file_path);
  lua_setfield(L, -2, "path");
  lua_pop(L,1);

  luaopen_ats(L);
  UrlRewrite::luaopen(L);
  lua_getglobal(L,"require");
  lua_pushstring(L,module);
  if(lua_pcall(L,1,1,0) != 0) {
    printf("[TrafficServer] failed to run lua config '%s'\n%s", config_file_path,
           lua_tostring(L,-1));
    exit(1);
  }
  lua_pop(L, lua_gettop(L));
  return L;
}

int
luaConfig::call(lua_State *L, const char *method, int nargs) {
  lua_getglobal(L, "tsconfig");
  lua_getfield(L, -1, method);
  if(lua_isnil(L,-1)) {
    lua_pop(L,2);
    return -1;
  }
  lua_remove(L,-2);
  lua_insert(L, 0 - (nargs+1));
  if(lua_pcall(L, nargs, 0, 0) != 0) {
    Error("lua call(%s) failed: %s\n", method, lua_tostring(L,-1));
    return -1;
  }
  return 0;
}

void luaConfigInit() {
  globalLuaConfig.boot();
  globalLuaConfig.records();
}
