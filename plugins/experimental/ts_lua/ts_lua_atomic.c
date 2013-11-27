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


#include "ts_lua_atomic.h"

#define INK_MEMORY_BARRIER

#define INK_QUEUE_LD64(dst,src) *((uint64_t*)&(dst)) = *((uint64_t*)&(src))
#define ADDRESS_OF_NEXT(x, offset) ((void **)((char *)x + offset))

#define FROM_PTR(_x) ((void*)(_x))
#define TO_PTR(_x) ((void*)(_x))
#define FREELIST_VERSION(_x) (((intptr_t)(_x))>>48)
#define FREELIST_POINTER(_x) ((void*)(((((intptr_t)(_x))<<16)>>16) | \
            (((~((((intptr_t)(_x))<<16>>63)-1))>>48)<<48)))
#define SET_FREELIST_POINTER_VERSION(_x,_p,_v) \
    _x = ((((intptr_t)(_p))&0x0000FFFFFFFFFFFFULL) | (((_v)&0xFFFFULL) << 48))


static inline int64_t ts_lua_atomic_cas64(volatile int64_t * mem, int64_t old, int64_t new_value);


void
ts_lua_atomiclist_init(ts_lua_atomiclist * l, const char *name, uint32_t offset_to_next)
{
    l->name = name;
    l->offset = offset_to_next;

    SET_FREELIST_POINTER_VERSION(l->head, FROM_PTR(0), 0);
}

void *
ts_lua_atomiclist_push(ts_lua_atomiclist * l, void *item)
{
    int64_t         head;
    int64_t         item_pair;
    int             result = 0;
    volatile void   *h = NULL;

    volatile void **adr_of_next = (volatile void **) ADDRESS_OF_NEXT(item, l->offset);

    do {
        INK_QUEUE_LD64(head, l->head);
        h = FREELIST_POINTER(head);
        *adr_of_next = h;
        SET_FREELIST_POINTER_VERSION(item_pair, FROM_PTR(item), FREELIST_VERSION(head));
        INK_MEMORY_BARRIER;

        result = ts_lua_atomic_cas64((int64_t *) & l->head, head, item_pair);
    } while (result == 0);

    return TO_PTR(h);
}

void *
ts_lua_atomiclist_popall(ts_lua_atomiclist * l)
{
    int64_t     item, next;
    void        *ret;
    int         result = 0;

    do {
        INK_QUEUE_LD64(item, l->head);
        if (TO_PTR(FREELIST_POINTER(item)) == NULL)
            return NULL;

        SET_FREELIST_POINTER_VERSION(next, FROM_PTR(NULL), FREELIST_VERSION(item) + 1);
        result = ts_lua_atomic_cas64((int64_t *) & l->head, item, next);
    } while (result == 0);

    ret = TO_PTR(FREELIST_POINTER(item)); 

    return ret;
}

static inline int64_t
ts_lua_atomic_cas64(volatile int64_t * mem, int64_t old, int64_t new_value)
{
    return __sync_bool_compare_and_swap(mem, old, new_value);
}


