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

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>
#include "ts_lua_coroutine.h"

#define TS_LUA_FUNCTION_REMAP "do_remap"
#define TS_LUA_FUNCTION_OS_RESPONSE "do_os_response"
#define TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE "do_cache_lookup_complete"
#define TS_LUA_FUNCTION_SEND_REQUEST "do_send_request"
#define TS_LUA_FUNCTION_READ_RESPONSE "do_read_response"
#define TS_LUA_FUNCTION_SEND_RESPONSE "do_send_response"
#define TS_LUA_FUNCTION_READ_REQUEST "do_read_request"
#define TS_LUA_FUNCTION_TXN_START "do_txn_start"
#define TS_LUA_FUNCTION_PRE_REMAP "do_pre_remap"
#define TS_LUA_FUNCTION_POST_REMAP "do_post_remap"
#define TS_LUA_FUNCTION_OS_DNS "do_os_dns"
#define TS_LUA_FUNCTION_READ_CACHE "do_read_cache"
#define TS_LUA_FUNCTION_TXN_CLOSE "do_txn_close"

#define TS_LUA_FUNCTION_G_SEND_REQUEST "do_global_send_request"
#define TS_LUA_FUNCTION_G_READ_REQUEST "do_global_read_request"
#define TS_LUA_FUNCTION_G_SEND_RESPONSE "do_global_send_response"
#define TS_LUA_FUNCTION_G_READ_RESPONSE "do_global_read_response"
#define TS_LUA_FUNCTION_G_CACHE_LOOKUP_COMPLETE "do_global_cache_lookup_complete"
#define TS_LUA_FUNCTION_G_TXN_START "do_global_txn_start"
#define TS_LUA_FUNCTION_G_PRE_REMAP "do_global_pre_remap"
#define TS_LUA_FUNCTION_G_POST_REMAP "do_global_post_remap"
#define TS_LUA_FUNCTION_G_OS_DNS "do_global_os_dns"
#define TS_LUA_FUNCTION_G_READ_CACHE "do_global_read_cache"
#define TS_LUA_FUNCTION_G_TXN_CLOSE "do_global_txn_close"

#define TS_LUA_DEBUG_TAG "ts_lua"

#define TS_LUA_EVENT_COROUTINE_CONT 20000

#define TS_LUA_MAX_SCRIPT_FNAME_LENGTH 1024
#define TS_LUA_MAX_CONFIG_VARS_COUNT 256
#define TS_LUA_MAX_SHARED_DICT_NAME_LENGTH 128
#define TS_LUA_MAX_SHARED_DICT_COUNT 32
#define TS_LUA_MAX_URL_LENGTH 2048
#define TS_LUA_MAX_OVEC_SIZE (3 * 32)
#define TS_LUA_MAX_RESIDENT_PCRE 64
#define TS_LUA_MAX_STR_LENGTH 2048

#define TS_LUA_MIN_ALIGN sizeof(void *)
#define TS_LUA_MEM_ALIGN(size) (((size) + ((TS_LUA_MIN_ALIGN)-1)) & ~((TS_LUA_MIN_ALIGN)-1))
#define TS_LUA_ALIGN_COUNT(size) (size / TS_LUA_MIN_ALIGN)

#define TS_LUA_MAKE_VAR_ITEM(X) \
  {                             \
    X, #X                       \
  }

/* for http config or cntl var */
typedef struct {
  int nvar;
  char *svar;
} ts_lua_var_item;

typedef struct {
  char *content;
  char script[TS_LUA_MAX_SCRIPT_FNAME_LENGTH];
  void *conf_vars[TS_LUA_MAX_CONFIG_VARS_COUNT];

  unsigned int _first : 1; // create current instance for 1st ts_lua_main_ctx
  unsigned int _last : 1;  // create current instance for the last ts_lua_main_ctx

  int remap;
  int states;
  int ljgc;
  int ref_count;

  int init_func;
} ts_lua_instance_conf;

/* lua state for http request */
typedef struct {
  ts_lua_cont_info cinfo;

  TSHttpTxn txnp;
  TSMBuffer client_request_bufp;
  TSMLoc client_request_hdrp;
  TSMLoc client_request_url;

  TSMBuffer server_request_bufp;
  TSMLoc server_request_hdrp;
  TSMLoc server_request_url;

  TSMBuffer server_response_bufp;
  TSMLoc server_response_hdrp;

  TSMBuffer client_response_bufp;
  TSMLoc client_response_hdrp;

  TSMBuffer cached_response_bufp;
  TSMLoc cached_response_hdrp;

  ts_lua_instance_conf *instance_conf;

  int has_hook;

  TSRemapRequestInfo *rri;

} ts_lua_http_ctx;

typedef struct {
  TSVIO vio;
  TSIOBuffer buffer;
  TSIOBufferReader reader;
} ts_lua_io_handle;

typedef struct {
  ts_lua_cont_info cinfo;

  ts_lua_io_handle output;
  ts_lua_io_handle reserved;

  ts_lua_http_ctx *hctx;
  int64_t upstream_bytes;
  int64_t upstream_watermark_bytes;
  int64_t downstream_bytes;
  int64_t total;

} ts_lua_http_transform_ctx;

typedef struct {
  ts_lua_cont_info cinfo;

  ts_lua_io_handle input;
  ts_lua_io_handle output;

  TSVConn net_vc;
  ts_lua_http_ctx *hctx;

  int64_t to_flush;
  unsigned int reuse : 1;
  unsigned int recv_complete : 1;
  unsigned int send_complete : 1;
  unsigned int all_ready : 1;
} ts_lua_http_intercept_ctx;

#define TS_LUA_RELEASE_IO_HANDLE(ih)    \
  do {                                  \
    if (ih->reader) {                   \
      TSIOBufferReaderFree(ih->reader); \
      ih->reader = NULL;                \
    }                                   \
    if (ih->buffer) {                   \
      TSIOBufferDestroy(ih->buffer);    \
      ih->buffer = NULL;                \
    }                                   \
  } while (0)

#ifndef ATS_UNUSED
#define ATS_UNUSED __attribute__((unused))
#endif
