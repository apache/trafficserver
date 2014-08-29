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


#ifndef _TS_LUA_UTIL_H
#define _TS_LUA_UTIL_H

#include "ts_lua_common.h"

int ts_lua_create_vm(ts_lua_main_ctx * arr, int n);
void ts_lua_destroy_vm(ts_lua_main_ctx * arr, int n);

int ts_lua_add_module(ts_lua_instance_conf * conf, ts_lua_main_ctx * arr, int n, int argc, char *argv[]);

int ts_lua_del_module(ts_lua_instance_conf * conf, ts_lua_main_ctx * arr, int n);

int ts_lua_init_instance(ts_lua_instance_conf * conf);
int ts_lua_del_instance(ts_lua_instance_conf * conf);

void ts_lua_set_instance_conf(lua_State * L, ts_lua_instance_conf * conf);
ts_lua_instance_conf *ts_lua_get_instance_conf(lua_State * L);

void ts_lua_set_http_ctx(lua_State * L, ts_lua_http_ctx * ctx);
ts_lua_http_ctx *ts_lua_get_http_ctx(lua_State * L);

ts_lua_http_ctx *ts_lua_create_http_ctx(ts_lua_main_ctx * mctx, ts_lua_instance_conf * conf);
void ts_lua_destroy_http_ctx(ts_lua_http_ctx * http_ctx);

void ts_lua_destroy_transform_ctx(ts_lua_transform_ctx * transform_ctx);

ts_lua_http_intercept_ctx *ts_lua_create_http_intercept_ctx(ts_lua_http_ctx * http_ctx);
ts_lua_http_intercept_ctx *ts_lua_get_http_intercept_ctx(lua_State * L);
void ts_lua_destroy_http_intercept_ctx(ts_lua_http_intercept_ctx * ictx);

int ts_lua_http_cont_handler(TSCont contp, TSEvent event, void *edata);

#endif
