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

#if !defined (_luaConfig_h_)
#define _luaConfig_h_

#include "ink_apidefs.h"
#include "lua.hpp"

/////////////////////////////////////////////////////////////
//
// class luaConfig
//
/////////////////////////////////////////////////////////////

void luaConfigInit();
void drop_lua_state_holder(void *);

struct luaConfigStateHolder {
public:
  luaConfigStateHolder() : active(0), uses_remaining(0) {
    states[0] = states[1] = NULL;
  }
  lua_State *states[2];
  int     active;
  int64_t uses_remaining;
};

class luaConfig
{
public:
  luaConfig(const char *mod) : config_module(mod) {
    ink_thread_key_create(&state_key, drop_lua_state_holder);
  }
  void boot();
  void records();
  inline lua_State *getL() { return getL(-1); }
  int call(lua_State *, const char *method, int nargs);
  inline int call(const char *method, int nargs) {
    return call(getL(), method, nargs);
  }

private:
  lua_State *open(const char *path, const char *module);
  inline void setL(int which, lua_State *L) {
    assert(which >= 0 && which < 2);
    struct luaConfigStateHolder *state_holder;
    state_holder = (struct luaConfigStateHolder *)ink_thread_getspecific(state_key);
    if(state_holder == NULL) {
      state_holder = new struct luaConfigStateHolder();
      ink_thread_setspecific(state_key, (void *)state_holder);
    }
    state_holder->states[which] = L;
  }
  inline lua_State *getL(int which) {
    assert(which >= -1 && which < 2);
    struct luaConfigStateHolder *state_holder;
    state_holder = (struct luaConfigStateHolder *)ink_thread_getspecific(state_key);
    if(state_holder == NULL) {
      state_holder = new struct luaConfigStateHolder();
      ink_thread_setspecific(state_key, (void *)state_holder);
    }
    return state_holder->states[(which < 0) ? state_holder->active : which];
  }
  ink_thread_key state_key;
  const char *config_module;
  struct luaConfigStateHolder state_holder;
};

extern luaConfig globalLuaConfig;

#endif /* _luaConfig_h_ */
