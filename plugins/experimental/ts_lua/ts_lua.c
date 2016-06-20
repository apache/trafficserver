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

#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "ts_lua_util.h"

#define TS_LUA_MAX_STATE_COUNT 256

static uint64_t ts_lua_http_next_id   = 0;
static uint64_t ts_lua_g_http_next_id = 0;

static ts_lua_main_ctx *ts_lua_main_ctx_array;
static ts_lua_main_ctx *ts_lua_g_main_ctx_array;

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  int ret;

  if (!api_info || api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  if (ts_lua_main_ctx_array != NULL)
    return TS_SUCCESS;

  ts_lua_main_ctx_array = TSmalloc(sizeof(ts_lua_main_ctx) * TS_LUA_MAX_STATE_COUNT);
  memset(ts_lua_main_ctx_array, 0, sizeof(ts_lua_main_ctx) * TS_LUA_MAX_STATE_COUNT);

  ret = ts_lua_create_vm(ts_lua_main_ctx_array, TS_LUA_MAX_STATE_COUNT);

  if (ret) {
    ts_lua_destroy_vm(ts_lua_main_ctx_array, TS_LUA_MAX_STATE_COUNT);
    TSfree(ts_lua_main_ctx_array);
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  int fn;
  int ret;

  if (argc < 3) {
    strncpy(errbuf, "[TSRemapNewInstance] - lua script file or string is required !!", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  fn = 1;

  if (argv[2][0] != '/') {
    fn = 0;
  } else if (strlen(argv[2]) >= TS_LUA_MAX_SCRIPT_FNAME_LENGTH - 16) {
    return TS_ERROR;
  }

  ts_lua_instance_conf *conf = TSmalloc(sizeof(ts_lua_instance_conf));
  if (!conf) {
    strncpy(errbuf, "[TSRemapNewInstance] TSmalloc failed!!", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  memset(conf, 0, sizeof(ts_lua_instance_conf));
  conf->remap = 1;

  if (fn) {
    snprintf(conf->script, TS_LUA_MAX_SCRIPT_FNAME_LENGTH, "%s", argv[2]);
  } else {
    conf->content = argv[2];
  }

  ts_lua_init_instance(conf);

  ret = ts_lua_add_module(conf, ts_lua_main_ctx_array, TS_LUA_MAX_STATE_COUNT, argc - 2, &argv[2], errbuf, errbuf_size);

  if (ret != 0) {
    return TS_ERROR;
  }

  *ih = conf;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  ts_lua_del_module((ts_lua_instance_conf *)ih, ts_lua_main_ctx_array, TS_LUA_MAX_STATE_COUNT);
  ts_lua_del_instance(ih);
  TSfree(ih);
  return;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  int ret;
  uint64_t req_id;

  TSCont contp;
  lua_State *L;

  ts_lua_main_ctx *main_ctx;
  ts_lua_http_ctx *http_ctx;
  ts_lua_cont_info *ci;

  ts_lua_instance_conf *instance_conf;

  instance_conf = (ts_lua_instance_conf *)ih;
  req_id        = __sync_fetch_and_add(&ts_lua_http_next_id, 1);

  main_ctx = &ts_lua_main_ctx_array[req_id % TS_LUA_MAX_STATE_COUNT];

  TSMutexLock(main_ctx->mutexp);

  http_ctx = ts_lua_create_http_ctx(main_ctx, instance_conf);

  http_ctx->txnp                = rh;
  http_ctx->client_request_bufp = rri->requestBufp;
  http_ctx->client_request_hdrp = rri->requestHdrp;
  http_ctx->client_request_url  = rri->requestUrl;
  http_ctx->rri                 = rri;
  http_ctx->remap               = 1;
  http_ctx->has_hook            = 0;

  ci = &http_ctx->cinfo;
  L  = ci->routine.lua;

  contp = TSContCreate(ts_lua_http_cont_handler, NULL);
  TSContDataSet(contp, http_ctx);

  ci->contp = contp;
  ci->mutex = TSContMutexGet((TSCont)rh);

  lua_getglobal(L, TS_LUA_FUNCTION_REMAP);
  if (lua_type(L, -1) != LUA_TFUNCTION) {
    TSMutexUnlock(main_ctx->mutexp);
    return TSREMAP_NO_REMAP;
  }

  ts_lua_set_cont_info(L, NULL);
  if (lua_pcall(L, 0, 1, 0) != 0) {
    TSError("[ts_lua] lua_pcall failed: %s", lua_tostring(L, -1));
    ret = TSREMAP_NO_REMAP;

  } else {
    ret = lua_tointeger(L, -1);
  }

  lua_pop(L, 1);

  if (http_ctx->has_hook) {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] has txn hook -> adding txn close hook handler to release resources", __FUNCTION__);
    TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);
  } else {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] no txn hook -> release resources now", __FUNCTION__);
    ts_lua_destroy_http_ctx(http_ctx);
  }

  TSMutexUnlock(main_ctx->mutexp);

  return ret;
}

static int
globalHookHandler(TSCont contp, TSEvent event ATS_UNUSED, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;

  int ret;
  uint64_t req_id;
  TSCont txn_contp;

  lua_State *l;

  ts_lua_main_ctx *main_ctx;
  ts_lua_http_ctx *http_ctx;
  ts_lua_cont_info *ci;

  ts_lua_instance_conf *conf = (ts_lua_instance_conf *)TSContDataGet(contp);

  req_id = __sync_fetch_and_add(&ts_lua_g_http_next_id, 1);

  main_ctx = &ts_lua_g_main_ctx_array[req_id % TS_LUA_MAX_STATE_COUNT];

  TSDebug(TS_LUA_DEBUG_TAG, "[%s] req_id: %" PRId64, __FUNCTION__, req_id);
  TSMutexLock(main_ctx->mutexp);

  http_ctx           = ts_lua_create_http_ctx(main_ctx, conf);
  http_ctx->txnp     = txnp;
  http_ctx->rri      = NULL;
  http_ctx->remap    = 0;
  http_ctx->has_hook = 0;

  if (!http_ctx->client_request_bufp) {
    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      http_ctx->client_request_bufp = bufp;
      http_ctx->client_request_hdrp = hdr_loc;

      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        http_ctx->client_request_url = url_loc;
      }
    }
  }

  if (!http_ctx->client_request_hdrp) {
    ts_lua_destroy_http_ctx(http_ctx);
    TSMutexUnlock(main_ctx->mutexp);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  txn_contp = TSContCreate(ts_lua_http_cont_handler, NULL);
  TSContDataSet(txn_contp, http_ctx);

  ci        = &http_ctx->cinfo;
  ci->contp = txn_contp;
  ci->mutex = TSContMutexGet((TSCont)txnp);

  l = ci->routine.lua;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    lua_getglobal(l, TS_LUA_FUNCTION_G_READ_REQUEST);
    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    lua_getglobal(l, TS_LUA_FUNCTION_G_SEND_REQUEST);
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    lua_getglobal(l, TS_LUA_FUNCTION_G_READ_RESPONSE);
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    lua_getglobal(l, TS_LUA_FUNCTION_G_SEND_RESPONSE);
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    lua_getglobal(l, TS_LUA_FUNCTION_G_CACHE_LOOKUP_COMPLETE);
    break;

  case TS_EVENT_HTTP_TXN_START:
    lua_getglobal(l, TS_LUA_FUNCTION_G_TXN_START);
    break;

  case TS_EVENT_HTTP_PRE_REMAP:
    lua_getglobal(l, TS_LUA_FUNCTION_G_PRE_REMAP);
    break;

  case TS_EVENT_HTTP_POST_REMAP:
    lua_getglobal(l, TS_LUA_FUNCTION_G_POST_REMAP);
    break;

  case TS_EVENT_HTTP_SELECT_ALT:
    lua_getglobal(l, TS_LUA_FUNCTION_G_SELECT_ALT);
    break;

  case TS_EVENT_HTTP_OS_DNS:
    lua_getglobal(l, TS_LUA_FUNCTION_G_OS_DNS);
    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:
    lua_getglobal(l, TS_LUA_FUNCTION_G_READ_CACHE);
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    lua_getglobal(l, TS_LUA_FUNCTION_G_TXN_CLOSE);
    break;

  default:
    ts_lua_destroy_http_ctx(http_ctx);
    TSMutexUnlock(main_ctx->mutexp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  if (lua_type(l, -1) != LUA_TFUNCTION) {
    lua_pop(l, 1);
    ts_lua_destroy_http_ctx(http_ctx);
    TSMutexUnlock(main_ctx->mutexp);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  ts_lua_set_cont_info(l, NULL);

  if (lua_pcall(l, 0, 1, 0) != 0) {
    TSError("[ts_lua] lua_pcall failed: %s", lua_tostring(l, -1));
  }

  ret = lua_tointeger(l, -1);
  lua_pop(l, 1);

  if (http_ctx->has_hook) {
    // add a hook to release resources for context
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] has txn hook -> adding txn close hook handler to release resources", __FUNCTION__);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
  } else {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] no txn hook -> release resources now", __FUNCTION__);
    ts_lua_destroy_http_ctx(http_ctx);
  }

  TSMutexUnlock(main_ctx->mutexp);

  if (ret) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  } else {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = "ts_lua";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[ts_lua] Plugin registration failed.");
  }

  int ret                 = 0;
  ts_lua_g_main_ctx_array = TSmalloc(sizeof(ts_lua_main_ctx) * TS_LUA_MAX_STATE_COUNT);
  memset(ts_lua_g_main_ctx_array, 0, sizeof(ts_lua_main_ctx) * TS_LUA_MAX_STATE_COUNT);

  ret = ts_lua_create_vm(ts_lua_g_main_ctx_array, TS_LUA_MAX_STATE_COUNT);

  if (ret) {
    ts_lua_destroy_vm(ts_lua_g_main_ctx_array, TS_LUA_MAX_STATE_COUNT);
    TSfree(ts_lua_g_main_ctx_array);
    return;
  }

  if (argc < 2) {
    TSError("[ts_lua][%s] lua script file required !!", __FUNCTION__);
    return;
  }

  if (strlen(argv[1]) >= TS_LUA_MAX_SCRIPT_FNAME_LENGTH - 16) {
    TSError("[ts_lua][%s] lua script file name too long !!", __FUNCTION__);
    return;
  }

  ts_lua_instance_conf *conf = TSmalloc(sizeof(ts_lua_instance_conf));
  if (!conf) {
    TSError("[ts_lua][%s] TSmalloc failed !!", __FUNCTION__);
    return;
  }
  memset(conf, 0, sizeof(ts_lua_instance_conf));
  conf->remap = 0;

  snprintf(conf->script, TS_LUA_MAX_SCRIPT_FNAME_LENGTH, "%s", argv[1]);

  ts_lua_init_instance(conf);

  char errbuf[TS_LUA_MAX_STR_LENGTH];
  int errbuf_len = sizeof(errbuf);
  ret = ts_lua_add_module(conf, ts_lua_g_main_ctx_array, TS_LUA_MAX_STATE_COUNT, argc - 1, (char **)&argv[1], errbuf, errbuf_len);

  if (ret != 0) {
    TSError(errbuf, NULL);
    TSError("[ts_lua][%s] ts_lua_add_module failed", __FUNCTION__);
    return;
  }

  TSCont global_contp = TSContCreate(globalHookHandler, NULL);
  if (!global_contp) {
    TSError("[ts_lua][%s] could not create transaction start continuation", __FUNCTION__);
    return;
  }
  TSContDataSet(global_contp, conf);

  // adding hook based on whether the lua global function exists.
  ts_lua_main_ctx *main_ctx = &ts_lua_g_main_ctx_array[0];
  ts_lua_http_ctx *http_ctx = ts_lua_create_http_ctx(main_ctx, conf);
  lua_State *l              = http_ctx->cinfo.routine.lua;

  lua_getglobal(l, TS_LUA_FUNCTION_G_SEND_REQUEST);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "send_request_hdr_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_READ_RESPONSE);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "read_response_hdr_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_SEND_RESPONSE);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "send_response_hdr_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_CACHE_LOOKUP_COMPLETE);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "cache_lookup_complete_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_READ_REQUEST);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "read_request_hdr_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_TXN_START);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "txn_start_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_PRE_REMAP);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_PRE_REMAP_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "pre_remap_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_POST_REMAP);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "post_remap_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_SELECT_ALT);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "select_alt_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_OS_DNS);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "os_dns_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_READ_CACHE);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "read_cache_hdr_hook added");
  }
  lua_pop(l, 1);

  lua_getglobal(l, TS_LUA_FUNCTION_G_TXN_CLOSE);
  if (lua_type(l, -1) == LUA_TFUNCTION) {
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, global_contp);
    TSDebug(TS_LUA_DEBUG_TAG, "txn_close_hook added");
  }
  lua_pop(l, 1);

  ts_lua_destroy_http_ctx(http_ctx);
}
