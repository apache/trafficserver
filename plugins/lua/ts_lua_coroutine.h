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

#ifndef _TS_LUA_COROUTINE_H
#define _TS_LUA_COROUTINE_H

#include <stdio.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <ts/ts.h>

struct async_item;
typedef int (*async_clean)(struct async_item *item);

/* main context*/
typedef struct {
  lua_State *lua; // basic lua vm, injected
  TSMutex mutexp; // mutex for lua vm
  int gref;       // reference for lua vm self, in reg table
} ts_lua_main_ctx;

/* coroutine */
typedef struct {
  ts_lua_main_ctx *mctx;
  lua_State *lua; // derived lua_thread
  int ref;        // reference for lua_thread, in REG Table
} ts_lua_coroutine;

/* continuation info */
typedef struct {
  ts_lua_coroutine routine;
  TSCont contp;                   // continuation for the routine
  TSMutex mutex;                  // mutex for continuation
  struct async_item *async_chain; // async_item list
} ts_lua_cont_info;

/* asynchronous item */
typedef struct async_item {
  struct async_item *next;
  ts_lua_cont_info *cinfo;

  TSCont contp; // continuation for the async operation
  void *data;   // private data

  async_clean cleanup; // cleanup function
  int deleted : 1;
} ts_lua_async_item;

ts_lua_async_item *ts_lua_async_create_item(TSCont cont, async_clean func, void *d, ts_lua_cont_info *ci);
void ts_lua_release_cont_info(ts_lua_cont_info *ci);

#endif
