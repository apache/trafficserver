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


#ifndef _TS_LUA_ATOMIC_H
#define _TS_LUA_ATOMIC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct
{
    volatile int64_t head;
    const char *name;
    unsigned int offset;
} ts_lua_atomiclist;


void ts_lua_atomiclist_init(ts_lua_atomiclist * l, const char *name, uint32_t offset_to_next);
void *ts_lua_atomiclist_push(ts_lua_atomiclist * l, void *item);
void *ts_lua_atomiclist_popall(ts_lua_atomiclist * l);

static inline int ts_lua_atomic_increment(volatile int32_t *mem, int value) { return __sync_fetch_and_add(mem, value); }

#endif

