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
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>

#include "ts_lua_util.h"

#define TS_LUA_MAX_STATE_COUNT 256

#define TS_LUA_STATS_TIMEOUT 5000   // 5s -- convert to configurable
#define TS_LUA_STATS_BUFFER_SIZE 10 // stats buffer

#define TS_LUA_IND_STATE 0
#define TS_LUA_IND_GC_BYTES 1
#define TS_LUA_IND_THREADS 2
#define TS_LUA_IND_SIZE 3

static uint64_t ts_lua_http_next_id   = 0;
static uint64_t ts_lua_g_http_next_id = 0;

static ts_lua_main_ctx *ts_lua_main_ctx_array   = NULL;
static ts_lua_main_ctx *ts_lua_g_main_ctx_array = NULL;

static pthread_key_t lua_g_state_key;
static pthread_key_t lua_state_key;

// records.config entry injected by plugin
static char const *const ts_lua_mgmt_state_str   = "proxy.config.plugin.lua.max_states";
static char const *const ts_lua_mgmt_state_regex = "^[1-9][0-9]*$";

// this is set the first time global configuration is probed.
static int ts_lua_max_state_count = 0;

// lifecycle message tag
static char const *const print_tag = "stats_print";
static char const *const reset_tag = "stats_reset";

// stat record strings
static char const *const ts_lua_stat_strs[] = {
  "plugin.lua.remap.states",
  "plugin.lua.remap.gc_bytes",
  "plugin.lua.remap.threads",
  NULL,
};
static char const *const ts_lua_g_stat_strs[] = {
  "plugin.lua.global.states",
  "plugin.lua.global.gc_bytes",
  "plugin.lua.global.threads",
  NULL,
};

typedef struct {
  ts_lua_main_ctx *main_ctx_array;

  int gc_kb;   // last collected gc in kb
  int threads; // last collected number active threads

  int stat_inds[TS_LUA_IND_SIZE]; // stats indices

} ts_lua_plugin_stats;

ts_lua_plugin_stats *
create_plugin_stats(ts_lua_main_ctx *const main_ctx_array, char const *const *stat_strs)
{
  ts_lua_plugin_stats *const stats = TSmalloc(sizeof(ts_lua_plugin_stats));
  memset(stats, 0, sizeof(ts_lua_plugin_stats));

  stats->main_ctx_array = main_ctx_array;

  // sample buffers
  stats->gc_kb   = 0;
  stats->threads = 0;

  int const max_state_count = ts_lua_max_state_count;

  for (int ind = 0; ind < TS_LUA_IND_SIZE; ++ind) {
    stats->stat_inds[ind] = TSStatCreate(stat_strs[ind], TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  }

  // initialize the number of states stat
  int const sid = stats->stat_inds[TS_LUA_IND_STATE];
  if (TS_ERROR != sid) {
    TSStatIntSet(sid, max_state_count);
  }

  return stats;
}

ts_lua_main_ctx *
create_lua_vms()
{
  ts_lua_main_ctx *ctx_array = NULL;

  // Inject the setting into records.config
  static bool ts_mgt_int_inserted = false;
  if (!ts_mgt_int_inserted) {
    if (TS_SUCCESS == TSMgmtIntCreate(TS_RECORDTYPE_CONFIG, ts_lua_mgmt_state_str, TS_LUA_MAX_STATE_COUNT,
                                      TS_RECORDUPDATE_RESTART_TS, TS_RECORDCHECK_INT, ts_lua_mgmt_state_regex,
                                      TS_RECORDACCESS_READ_ONLY)) {
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] registered config string %s: with default [%d]", __FUNCTION__, ts_lua_mgmt_state_str,
              TS_LUA_MAX_STATE_COUNT);
    } else {
      TSError("[%s][%s] failed to register %s", TS_LUA_DEBUG_TAG, __FUNCTION__, ts_lua_mgmt_state_str);
    }
    ts_mgt_int_inserted = true;
  }

  if (0 == ts_lua_max_state_count) {
    TSMgmtInt mgmt_state = 0;

    if (TS_SUCCESS != TSMgmtIntGet(ts_lua_mgmt_state_str, &mgmt_state)) {
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] setting max state to default: %d", __FUNCTION__, TS_LUA_MAX_STATE_COUNT);
      ts_lua_max_state_count = TS_LUA_MAX_STATE_COUNT;
    } else {
      ts_lua_max_state_count = (int)mgmt_state;
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] found %s: [%d]", __FUNCTION__, ts_lua_mgmt_state_str, ts_lua_max_state_count);
    }

    if (ts_lua_max_state_count < 1) {
      TSError("[ts_lua][%s] invalid %s: %d", __FUNCTION__, ts_lua_mgmt_state_str, ts_lua_max_state_count);
      ts_lua_max_state_count = 0;
      return NULL;
    }
  }

  ctx_array = TSmalloc(sizeof(ts_lua_main_ctx) * ts_lua_max_state_count);
  memset(ctx_array, 0, sizeof(ts_lua_main_ctx) * ts_lua_max_state_count);

  int const ret = ts_lua_create_vm(ctx_array, ts_lua_max_state_count);

  if (ret) {
    ts_lua_destroy_vm(ctx_array, ts_lua_max_state_count);
    TSfree(ctx_array);
    ctx_array = NULL;
    return NULL;
  }

  // Initialize the GC numbers, no need to lock here
  for (int index = 0; index < ts_lua_max_state_count; ++index) {
    ts_lua_main_ctx *const main_ctx = (ctx_array + index);
    lua_State *const lstate         = main_ctx->lua;
    ts_lua_ctx_stats *const stats   = main_ctx->stats;

    stats->gc_kb = stats->gc_kb_max = lua_getgccount(lstate);
  }

  return ctx_array;
}

// dump exhaustive per state summary stats
static void
collectStats(ts_lua_plugin_stats *const plugin_stats)
{
  TSMgmtInt gc_kb_total   = 0;
  TSMgmtInt threads_total = 0;

  ts_lua_main_ctx *const main_ctx_array = plugin_stats->main_ctx_array;

  // aggregate stats on the states
  for (int index = 0; index < ts_lua_max_state_count; ++index) {
    ts_lua_main_ctx *const main_ctx = (main_ctx_array + index);
    if (NULL != main_ctx) {
      ts_lua_ctx_stats *const stats = main_ctx->stats;

      TSMutexLock(stats->mutexp);
      gc_kb_total += (TSMgmtInt)stats->gc_kb;
      threads_total += (TSMgmtInt)stats->threads;
      TSMutexUnlock(stats->mutexp);
    }
  }

  // set the stats sample slot
  plugin_stats->gc_kb   = gc_kb_total;
  plugin_stats->threads = threads_total;
}

static void
publishStats(ts_lua_plugin_stats *const plugin_stats)
{
  TSMgmtInt const gc_bytes = plugin_stats->gc_kb * 1024;
  TSStatIntSet(plugin_stats->stat_inds[TS_LUA_IND_GC_BYTES], gc_bytes);
  TSStatIntSet(plugin_stats->stat_inds[TS_LUA_IND_THREADS], plugin_stats->threads);
}

// dump exhaustive per state summary stats
static int
statsHandler(TSCont contp, TSEvent event, void *edata)
{
  ts_lua_plugin_stats *const plugin_stats = (ts_lua_plugin_stats *)TSContDataGet(contp);

  collectStats(plugin_stats);
  publishStats(plugin_stats);

  TSContScheduleOnPool(contp, TS_LUA_STATS_TIMEOUT, TS_THREAD_POOL_TASK);

  return TS_EVENT_NONE;
}

static void
get_time_now_str(char *const buf, size_t const buflen)
{
  TSHRTime const timenowusec = TShrtime();
  int64_t const timemsec     = (int64_t)(timenowusec / 1000000);
  time_t const timesec       = (time_t)(timemsec / 1000);
  int const ms               = (int)(timemsec % 1000);

  struct tm tm;
  gmtime_r(&timesec, &tm);
  size_t const dtlen = strftime(buf, buflen, "%b %e %H:%M:%S", &tm);

  // tack on the ms
  snprintf(buf + dtlen, buflen - dtlen, ".%03d", ms);
}

// dump exhaustive per state summary stats
static int
lifecycleHandler(TSCont contp, TSEvent event, void *edata)
{
  // ensure the message is for ts_lua
  TSPluginMsg *const msgp = (TSPluginMsg *)edata;
  if (0 != strncasecmp(msgp->tag, TS_LUA_DEBUG_TAG, strlen(msgp->tag))) {
    return TS_EVENT_NONE;
  }

  ts_lua_main_ctx *const main_ctx_array = (ts_lua_main_ctx *)TSContDataGet(contp);

  static char const *const remapstr  = "remap";
  static char const *const globalstr = "global";

  char const *labelstr = NULL;

  if (main_ctx_array == ts_lua_main_ctx_array) {
    labelstr = remapstr;
  } else {
    labelstr = globalstr;
  }

  char timebuf[128];
  get_time_now_str(timebuf, 128);

  char const *const msgstr = (char *)msgp->data;
  enum State { Print, Reset } state;
  state                      = Print;
  size_t const reset_tag_len = strlen(reset_tag);

  if (reset_tag_len <= msgp->data_size && 0 == strncasecmp(reset_tag, msgstr, reset_tag_len)) {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] LIFECYCLE_MSG: %s", __FUNCTION__, reset_tag);
    state = Reset;
    fprintf(stderr, "[%s] %s (%s) resetting per state gc_kb_max and threads_max\n", timebuf, TS_LUA_DEBUG_TAG, labelstr);
  } else {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] LIFECYCLE_MSG: %s", __FUNCTION__, print_tag);
  }

  for (int index = 0; index < ts_lua_max_state_count; ++index) {
    ts_lua_main_ctx *const main_ctx = (main_ctx_array + index);
    if (NULL != main_ctx) {
      ts_lua_ctx_stats *const stats = main_ctx->stats;
      if (NULL != main_ctx) {
        TSMutexLock(stats->mutexp);

        switch (state) {
        case Reset:
          stats->threads_max = stats->threads;
          stats->gc_kb_max   = stats->gc_kb;
          break;

        case Print:
        default:
          fprintf(stderr, "[%s] %s (%s) id: %3d gc_kb: %6d gc_kb_max: %6d threads: %4d threads_max: %4d\n", timebuf,
                  TS_LUA_DEBUG_TAG, labelstr, index, stats->gc_kb, stats->gc_kb_max, stats->threads, stats->threads_max);
          break;
        }

        TSMutexUnlock(stats->mutexp);
      }
    }
  }

  return TS_EVENT_NONE;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info || api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  if (NULL == ts_lua_main_ctx_array) {
    ts_lua_main_ctx_array = create_lua_vms();
    if (NULL != ts_lua_main_ctx_array) {
      pthread_key_create(&lua_state_key, NULL);

      TSCont const lcontp = TSContCreate(lifecycleHandler, TSMutexCreate());
      TSContDataSet(lcontp, ts_lua_main_ctx_array);
      TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, lcontp);

      ts_lua_plugin_stats *const plugin_stats = create_plugin_stats(ts_lua_main_ctx_array, ts_lua_stat_strs);

      // start the stats management
      if (NULL != plugin_stats) {
        TSDebug(TS_LUA_DEBUG_TAG, "Starting up stats management continuation");
        TSCont const scontp = TSContCreate(statsHandler, TSMutexCreate());
        TSContDataSet(scontp, plugin_stats);
        TSContScheduleOnPool(scontp, TS_LUA_STATS_TIMEOUT, TS_THREAD_POOL_TASK);
      }
    } else {
      return TS_ERROR;
    }
  }

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  int ret;
  char script[TS_LUA_MAX_SCRIPT_FNAME_LENGTH];
  char *inline_script                  = "";
  int fn                               = 0;
  int states                           = ts_lua_max_state_count;
  int ljgc                             = 0;
  static const struct option longopt[] = {
    {"states", required_argument, 0, 's'},
    {"inline", required_argument, 0, 'i'},
    {"ljgc", required_argument, 0, 'g'},
    {0, 0, 0, 0},
  };

  argc--;
  argv++;

  for (;;) {
    int opt;

    opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);
    switch (opt) {
    case 's':
      states = atoi(optarg);
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] setting number of lua VMs [%d]", __FUNCTION__, states);
      // set state
      break;
    case 'i':
      inline_script = optarg;
      break;
    case 'g':
      ljgc = atoi(optarg);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (states < 1 || ts_lua_max_state_count < states) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - invalid state in option input. Must be between 1 and %d",
             ts_lua_max_state_count);
    return TS_ERROR;
  }

  if (argc - optind > 0) {
    fn = 1;
    if (argv[optind][0] == '/') {
      snprintf(script, sizeof(script), "%s", argv[optind]);
    } else {
      snprintf(script, sizeof(script), "%s/%s", TSConfigDirGet(), argv[optind]);
    }
  }

  if (strlen(inline_script) == 0 && argc - optind < 1) {
    strncpy(errbuf, "[TSRemapNewInstance] - lua script file or string is required !!", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  if (strlen(script) >= TS_LUA_MAX_SCRIPT_FNAME_LENGTH - 16) {
    strncpy(errbuf, "[TSRemapNewInstance] - lua script file name too long !!", errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
    return TS_ERROR;
  }

  ts_lua_instance_conf *conf = NULL;

  // check to make sure it is a lua file and there is no parameter for the lua file
  if (fn && (argc - optind < 2)) {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] checking if script has been registered", __FUNCTION__);

    // we only need to check the first lua VM for script registration
    TSMutexLock(ts_lua_main_ctx_array[0].mutexp);
    conf = ts_lua_script_registered(ts_lua_main_ctx_array[0].lua, script);
    TSMutexUnlock(ts_lua_main_ctx_array[0].mutexp);
  }

  if (!conf) {
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] creating new conf instance", __FUNCTION__);

    conf = TSmalloc(sizeof(ts_lua_instance_conf));
    if (!conf) {
      strncpy(errbuf, "[TSRemapNewInstance] TSmalloc failed!!", errbuf_size - 1);
      errbuf[errbuf_size - 1] = '\0';
      return TS_ERROR;
    }

    memset(conf, 0, sizeof(ts_lua_instance_conf));
    conf->states    = states;
    conf->remap     = 1;
    conf->init_func = 0;
    conf->ref_count = 1;
    conf->ljgc      = ljgc;

    TSDebug(TS_LUA_DEBUG_TAG, "Reference Count = %d , creating new instance...", conf->ref_count);

    if (fn) {
      snprintf(conf->script, TS_LUA_MAX_SCRIPT_FNAME_LENGTH, "%s", script);
    } else {
      conf->content = inline_script;
    }

    ts_lua_init_instance(conf);

    ret = ts_lua_add_module(conf, ts_lua_main_ctx_array, conf->states, argc - optind, &argv[optind], errbuf, errbuf_size);

    if (ret != 0) {
      return TS_ERROR;
    }

    // register the script only if it is from a file and has no __init__ function
    if (fn && !conf->init_func) {
      // we only need to register the script for the first lua VM
      TSMutexLock(ts_lua_main_ctx_array[0].mutexp);
      ts_lua_script_register(ts_lua_main_ctx_array[0].lua, conf->script, conf);
      TSMutexUnlock(ts_lua_main_ctx_array[0].mutexp);
    }
  } else {
    conf->ref_count++;
    TSDebug(TS_LUA_DEBUG_TAG, "Reference Count = %d , reference existing instance...", conf->ref_count);
  }

  *ih = conf;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  int states = ((ts_lua_instance_conf *)ih)->states;
  ts_lua_del_module((ts_lua_instance_conf *)ih, ts_lua_main_ctx_array, states);
  ts_lua_del_instance(ih);
  ((ts_lua_instance_conf *)ih)->ref_count--;
  if (((ts_lua_instance_conf *)ih)->ref_count == 0) {
    TSDebug(TS_LUA_DEBUG_TAG, "Reference Count = %d , freeing...", ((ts_lua_instance_conf *)ih)->ref_count);
    TSfree(ih);
  } else {
    TSDebug(TS_LUA_DEBUG_TAG, "Reference Count = %d , not freeing...", ((ts_lua_instance_conf *)ih)->ref_count);
  }
  return;
}

static TSRemapStatus
ts_lua_remap_plugin_init(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  int ret;
  uint64_t req_id;

  TSCont contp;
  lua_State *L;

  ts_lua_main_ctx *main_ctx;
  ts_lua_http_ctx *http_ctx;
  ts_lua_cont_info *ci;

  ts_lua_instance_conf *instance_conf;

  int remap     = (rri == NULL ? 0 : 1);
  instance_conf = (ts_lua_instance_conf *)ih;

  main_ctx = pthread_getspecific(lua_state_key);
  if (main_ctx == NULL) {
    req_id   = __sync_fetch_and_add(&ts_lua_http_next_id, 1);
    main_ctx = &ts_lua_main_ctx_array[req_id % instance_conf->states];
    pthread_setspecific(lua_state_key, main_ctx);
  }

  TSMutexLock(main_ctx->mutexp);

  http_ctx = ts_lua_create_http_ctx(main_ctx, instance_conf);

  http_ctx->txnp     = rh;
  http_ctx->has_hook = 0;
  http_ctx->rri      = rri;
  if (rri != NULL) {
    http_ctx->client_request_bufp = rri->requestBufp;
    http_ctx->client_request_hdrp = rri->requestHdrp;
    http_ctx->client_request_url  = rri->requestUrl;
  }

  ci = &http_ctx->cinfo;
  L  = ci->routine.lua;

  contp = TSContCreate(ts_lua_http_cont_handler, NULL);
  TSContDataSet(contp, http_ctx);

  ci->contp = contp;
  ci->mutex = TSContMutexGet((TSCont)rh);

  lua_getglobal(L, (remap ? TS_LUA_FUNCTION_REMAP : TS_LUA_FUNCTION_OS_RESPONSE));
  if (lua_type(L, -1) != LUA_TFUNCTION) {
    lua_pop(L, 1);
    ts_lua_destroy_http_ctx(http_ctx);
    TSMutexUnlock(main_ctx->mutexp);
    return TSREMAP_NO_REMAP;
  }

  ts_lua_set_cont_info(L, NULL);
  if (lua_pcall(L, 0, 1, 0) != 0) {
    TSError("[ts_lua][%s] lua_pcall failed: %s", __FUNCTION__, lua_tostring(L, -1));
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

void
TSRemapOSResponse(void *ih, TSHttpTxn rh, int os_response_type)
{
  TSDebug(TS_LUA_DEBUG_TAG, "[%s] os response function and type - %d", __FUNCTION__, os_response_type);
  ts_lua_remap_plugin_init(ih, rh, NULL);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  TSDebug(TS_LUA_DEBUG_TAG, "[%s] remap function", __FUNCTION__);
  return ts_lua_remap_plugin_init(ih, rh, rri);
}

static int
configHandler(TSCont contp, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  TSDebug(TS_LUA_DEBUG_TAG, "[%s] calling configuration handler", __FUNCTION__);
  ts_lua_instance_conf *conf = (ts_lua_instance_conf *)TSContDataGet(contp);
  ts_lua_reload_module(conf, ts_lua_g_main_ctx_array, conf->states);
  return 0;
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

  main_ctx = pthread_getspecific(lua_g_state_key);
  if (main_ctx == NULL) {
    req_id = __sync_fetch_and_add(&ts_lua_g_http_next_id, 1);
    TSDebug(TS_LUA_DEBUG_TAG, "[%s] req_id: %" PRId64, __FUNCTION__, req_id);
    main_ctx = &ts_lua_g_main_ctx_array[req_id % conf->states];
    pthread_setspecific(lua_g_state_key, main_ctx);
  }

  TSMutexLock(main_ctx->mutexp);

  http_ctx           = ts_lua_create_http_ctx(main_ctx, conf);
  http_ctx->txnp     = txnp;
  http_ctx->rri      = NULL;
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
    // client response can be changed within a transaction
    // (e.g. due to the follow redirect feature). So, clearing the pointers
    // to allow API(s) to fetch the pointers again when it re-enters the hook
    if (http_ctx->client_response_hdrp != NULL) {
      TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
      http_ctx->client_response_bufp = NULL;
      http_ctx->client_response_hdrp = NULL;
    }
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
    TSError("[ts_lua][%s] lua_pcall failed: %s", __FUNCTION__, lua_tostring(l, -1));
  }

  ret = lua_tointeger(l, -1);
  lua_pop(l, 1);

  // client response can be changed within a transaction
  // (e.g. due to the follow redirect feature). So, clearing the pointers
  // to allow API(s) to fetch the pointers again when it re-enters the hook
  if (http_ctx->client_response_hdrp != NULL) {
    TSHandleMLocRelease(http_ctx->client_response_bufp, TS_NULL_MLOC, http_ctx->client_response_hdrp);
    http_ctx->client_response_bufp = NULL;
    http_ctx->client_response_hdrp = NULL;
  }

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
    TSError("[ts_lua][%s] Plugin registration failed", __FUNCTION__);
  }

  if (NULL == ts_lua_g_main_ctx_array) {
    ts_lua_g_main_ctx_array = create_lua_vms();
    if (NULL != ts_lua_g_main_ctx_array) {
      pthread_key_create(&lua_g_state_key, NULL);

      TSCont const contp = TSContCreate(lifecycleHandler, TSMutexCreate());
      TSContDataSet(contp, ts_lua_g_main_ctx_array);
      TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);

      ts_lua_plugin_stats *const plugin_stats = create_plugin_stats(ts_lua_g_main_ctx_array, ts_lua_g_stat_strs);

      if (NULL != plugin_stats) {
        TSCont const scontp = TSContCreate(statsHandler, TSMutexCreate());
        TSContDataSet(scontp, plugin_stats);
        TSContScheduleOnPool(scontp, TS_LUA_STATS_TIMEOUT, TS_THREAD_POOL_TASK);
      }
    } else {
      return;
    }
  }

  int states = ts_lua_max_state_count;

  int reload                           = 0;
  static const struct option longopt[] = {
    {"states", required_argument, 0, 's'},
    {"enable-reload", no_argument, 0, 'r'},
    {0, 0, 0, 0},
  };

  for (;;) {
    int opt;

    opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);
    switch (opt) {
    case 's':
      states = atoi(optarg);
      // set state
      break;
    case 'r':
      reload = 1;
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] enable global plugin reload [%d]", __FUNCTION__, reload);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (states < 1 || ts_lua_max_state_count < states) {
    TSError("[ts_lua][%s] invalid # of states from option input. Must be between 1 and %d", __FUNCTION__, ts_lua_max_state_count);
    return;
  }

  if (argc - optind < 1) {
    TSError("[ts_lua][%s] lua script file required !!", __FUNCTION__);
    return;
  }

  if (strlen(argv[optind]) >= TS_LUA_MAX_SCRIPT_FNAME_LENGTH - 16) {
    TSError("[ts_lua][%s] lua script file name too long !!", __FUNCTION__);
    return;
  }

  ts_lua_instance_conf *conf = TSmalloc(sizeof(ts_lua_instance_conf));
  if (!conf) {
    TSError("[ts_lua][%s] TSmalloc failed !!", __FUNCTION__);
    return;
  }
  memset(conf, 0, sizeof(ts_lua_instance_conf));
  conf->remap  = 0;
  conf->states = states;

  if (argv[optind][0] == '/') {
    snprintf(conf->script, TS_LUA_MAX_SCRIPT_FNAME_LENGTH, "%s", argv[optind]);
  } else {
    snprintf(conf->script, TS_LUA_MAX_SCRIPT_FNAME_LENGTH, "%s/%s", TSConfigDirGet(), argv[optind]);
  }

  ts_lua_init_instance(conf);

  char errbuf[TS_LUA_MAX_STR_LENGTH];
  int const errbuf_len = sizeof(errbuf);
  int const ret =
    ts_lua_add_module(conf, ts_lua_g_main_ctx_array, conf->states, argc - optind, (char **)&argv[optind], errbuf, errbuf_len);

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

  // support for reload as global plugin
  if (reload) {
    TSCont config_contp = TSContCreate(configHandler, NULL);
    if (!config_contp) {
      TSError("[ts_lua][%s] could not create configuration continuation", __FUNCTION__);
      return;
    }
    TSContDataSet(config_contp, conf);

    TSMgmtUpdateRegister(config_contp, "ts_lua");
  }
}
