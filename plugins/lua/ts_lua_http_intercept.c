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
#include "ts_lua_http_intercept.h"

static int ts_lua_http_intercept(lua_State *L);
static int ts_lua_http_server_intercept(lua_State *L);
static int ts_lua_http_intercept_entry(TSCont contp, TSEvent event, void *edata);
static void ts_lua_http_intercept_process(ts_lua_http_intercept_ctx *ictx, TSVConn conn);
static void ts_lua_http_intercept_setup_read(ts_lua_http_intercept_ctx *ictx);
static void ts_lua_http_intercept_setup_write(ts_lua_http_intercept_ctx *ictx);
static int ts_lua_http_intercept_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_http_intercept_run_coroutine(ts_lua_http_intercept_ctx *ictx, int n);
static int ts_lua_http_intercept_process_read(TSEvent event, ts_lua_http_intercept_ctx *ictx);
static int ts_lua_http_intercept_process_write(TSEvent event, ts_lua_http_intercept_ctx *ictx);

static int ts_lua_say(lua_State *L);
static int ts_lua_flush(lua_State *L);
static int ts_lua_flush_wakeup(ts_lua_http_intercept_ctx *ictx);
static int ts_lua_flush_wakeup_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_flush_cleanup(ts_lua_async_item *ai);

void
ts_lua_inject_http_intercept_api(lua_State *L)
{
  /* ts.intercept */
  lua_pushcfunction(L, ts_lua_http_intercept);
  lua_setfield(L, -2, "intercept");

  /* ts.server_intercept */
  lua_pushcfunction(L, ts_lua_http_server_intercept);
  lua_setfield(L, -2, "server_intercept");
}

void
ts_lua_inject_intercept_api(lua_State *L)
{
  /*  ts.say(...) */
  lua_pushcfunction(L, ts_lua_say);
  lua_setfield(L, -2, "say");

  /*  ts.flush(...) */
  lua_pushcfunction(L, ts_lua_flush);
  lua_setfield(L, -2, "flush");
}

static int
ts_lua_http_intercept(lua_State *L)
{
  TSCont contp;
  int type, n;
  ts_lua_http_ctx *http_ctx;
  ts_lua_http_intercept_ctx *ictx;

  GET_HTTP_CONTEXT(http_ctx, L);

  n = lua_gettop(L);

  if (n < 1) {
    TSError("[ts_lua] ts.http.intercept need at least one param");
    return 0;
  }

  type = lua_type(L, 1);
  if (type != LUA_TFUNCTION) {
    TSError("[ts_lua] ts.http.intercept should use function as param, but there is %s", lua_typename(L, type));
    return 0;
  }

  ictx  = ts_lua_create_http_intercept_ctx(L, http_ctx, n);
  contp = TSContCreate(ts_lua_http_intercept_entry, TSMutexCreate());
  TSContDataSet(contp, ictx);

  TSHttpTxnIntercept(contp, http_ctx->txnp);
  http_ctx->has_hook = 1;

  return 0;
}

static int
ts_lua_http_server_intercept(lua_State *L)
{
  TSCont contp;
  int type, n;
  ts_lua_http_ctx *http_ctx;
  ts_lua_http_intercept_ctx *ictx;

  GET_HTTP_CONTEXT(http_ctx, L);

  n = lua_gettop(L);

  if (n < 1) {
    TSError("[ts_lua] ts.http.server_intercept need at least one param");
    return 0;
  }

  type = lua_type(L, 1);
  if (type != LUA_TFUNCTION) {
    TSError("[ts_lua] ts.http.server_intercept should use function as param, but there is %s", lua_typename(L, type));
    return 0;
  }

  ictx  = ts_lua_create_http_intercept_ctx(L, http_ctx, n);
  contp = TSContCreate(ts_lua_http_intercept_entry, TSMutexCreate());
  TSContDataSet(contp, ictx);

  TSHttpTxnServerIntercept(contp, http_ctx->txnp);
  http_ctx->has_hook = 1;

  return 0;
}

static int
ts_lua_http_intercept_entry(TSCont contp, TSEvent event, void *edata)
{
  ts_lua_http_intercept_ctx *ictx;

  ictx = (ts_lua_http_intercept_ctx *)TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_NET_ACCEPT_FAILED:
    if (edata) {
      TSVConnClose((TSVConn)edata);
    }

    ts_lua_destroy_http_intercept_ctx(ictx);
    break;

  case TS_EVENT_NET_ACCEPT:
    ts_lua_http_intercept_process(ictx, (TSVConn)edata);
    break;

  default:
    break;
  }

  TSContDestroy(contp);
  return 0;
}

static void
ts_lua_http_intercept_process(ts_lua_http_intercept_ctx *ictx, TSVConn conn)
{
  int n;
  TSCont contp;
  lua_State *L;
  TSMutex mtxp;
  ts_lua_cont_info *ci;

  ci   = &ictx->cinfo;
  mtxp = ictx->cinfo.routine.mctx->mutexp;

  contp = TSContCreate(ts_lua_http_intercept_handler, TSMutexCreate());
  TSContDataSet(contp, ictx);

  ci->contp = contp;
  ci->mutex = TSContMutexGet(contp);

  ictx->net_vc = conn;

  // set up read.
  ts_lua_http_intercept_setup_read(ictx);

  // set up write.
  ts_lua_http_intercept_setup_write(ictx);

  // invoke function here
  L = ci->routine.lua;

  TSMutexLock(mtxp);

  n = lua_gettop(L);

  ts_lua_http_intercept_run_coroutine(ictx, n - 1);

  TSMutexUnlock(mtxp);
}

static void
ts_lua_http_intercept_setup_read(ts_lua_http_intercept_ctx *ictx)
{
  ictx->input.buffer = TSIOBufferCreate();
  ictx->input.reader = TSIOBufferReaderAlloc(ictx->input.buffer);
  ictx->input.vio    = TSVConnRead(ictx->net_vc, ictx->cinfo.contp, ictx->input.buffer, INT64_MAX);
}

static void
ts_lua_http_intercept_setup_write(ts_lua_http_intercept_ctx *ictx)
{
  ictx->output.buffer = TSIOBufferCreate();
  ictx->output.reader = TSIOBufferReaderAlloc(ictx->output.buffer);
  ictx->output.vio    = TSVConnWrite(ictx->net_vc, ictx->cinfo.contp, ictx->output.reader, INT64_MAX);
}

static int
ts_lua_http_intercept_handler(TSCont contp, TSEvent event, void *edata)
{
  int ret, n;
  TSMutex mtxp;
  ts_lua_http_intercept_ctx *ictx;

  ictx = (ts_lua_http_intercept_ctx *)TSContDataGet(contp);
  mtxp = NULL;

  if (edata == ictx->input.vio) {
    ret = ts_lua_http_intercept_process_read(event, ictx);

  } else if (edata == ictx->output.vio) {
    ret = ts_lua_http_intercept_process_write(event, ictx);

  } else {
    mtxp = ictx->cinfo.routine.mctx->mutexp;
    n    = (intptr_t)edata;

    TSMutexLock(mtxp);
    ret = ts_lua_http_intercept_run_coroutine(ictx, n);
    TSMutexUnlock(mtxp);
  }

  if (ret || (ictx->send_complete && ictx->recv_complete)) {
    ts_lua_destroy_http_intercept_ctx(ictx);
  }

  return 0;
}

static int
ts_lua_http_intercept_run_coroutine(ts_lua_http_intercept_ctx *ictx, int n)
{
  int ret;
  int64_t avail;
  int64_t done;
  ts_lua_cont_info *ci;
  lua_State *L;

  ci = &ictx->cinfo;
  L  = ci->routine.lua;

  ts_lua_set_cont_info(L, ci);
  ret = lua_resume(L, n);

  switch (ret) {
  case 0: // finished
    avail = TSIOBufferReaderAvail(ictx->output.reader);
    done  = TSVIONDoneGet(ictx->output.vio);
    TSVIONBytesSet(ictx->output.vio, avail + done);
    ictx->all_ready = 1;

    if (avail) {
      TSVIOReenable(ictx->output.vio);
    } else {
      ictx->send_complete = 1;
    }

    break;

  case 1: // yield
    break;

  default: // error
    TSError("[ts_lua] lua_resume failed: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return -1;
  }

  return 0;
}

static int
ts_lua_http_intercept_process_read(TSEvent event, ts_lua_http_intercept_ctx *ictx)
{
  int64_t avail = TSIOBufferReaderAvail(ictx->input.reader);
  TSIOBufferReaderConsume(ictx->input.reader, avail);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    TSVConnShutdown(ictx->net_vc, 1, 0);
    ictx->recv_complete = 1;
    break; // READ_READY_READY is not equal to EOS break statement is probably missing?
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    ictx->recv_complete = 1;
    break;

  default:
    return -1;
  }

  return 0;
}

static int
ts_lua_http_intercept_process_write(TSEvent event, ts_lua_http_intercept_ctx *ictx)
{
  int64_t done, avail;

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:

    avail = TSIOBufferReaderAvail(ictx->output.reader);

    if (ictx->all_ready) {
      TSVIOReenable(ictx->output.vio);

    } else if (ictx->to_flush > 0) { // ts.flush()

      done = TSVIONDoneGet(ictx->output.vio);

      if (ictx->to_flush > done) {
        TSVIOReenable(ictx->output.vio);

      } else { // we had flush all the data we want
        ictx->to_flush = 0;
        ts_lua_flush_wakeup(ictx); // wake up
      }

    } else if (avail > 0) {
      TSVIOReenable(ictx->output.vio);
    }

    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    ictx->send_complete = 1;
    break;

  case TS_EVENT_ERROR:
  default:
    return -1;
  }

  return 0;
}

static int
ts_lua_say(lua_State *L)
{
  const char *data;
  size_t len;
  ts_lua_http_intercept_ctx *ictx;

  ictx = ts_lua_get_http_intercept_ctx(L);
  if (ictx == NULL) {
    TSError("[ts_lua] missing ictx");
    return 0;
  }

  data = luaL_checklstring(L, 1, &len);

  if (len > 0) {
    TSIOBufferWrite(ictx->output.buffer, data, len);
    TSVIOReenable(ictx->output.vio);
  }

  return 0;
}

static int
ts_lua_flush(lua_State *L)
{
  int64_t avail;
  ts_lua_http_intercept_ctx *ictx;

  ictx = ts_lua_get_http_intercept_ctx(L);
  if (ictx == NULL) {
    TSError("[ts_lua] missing ictx");
    return 0;
  }

  avail = TSIOBufferReaderAvail(ictx->output.reader);

  if (avail > 0) {
    ictx->to_flush = TSVIONDoneGet(ictx->output.vio) + TSIOBufferReaderAvail(ictx->output.reader);
    TSVIOReenable(ictx->output.vio);

    return lua_yield(L, 0);
  }

  return 0;
}

static int
ts_lua_flush_wakeup(ts_lua_http_intercept_ctx *ictx)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;
  TSAction action;
  TSCont contp;

  ci = &ictx->cinfo;

  contp  = TSContCreate(ts_lua_flush_wakeup_handler, ci->mutex);
  action = TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);

  ai = ts_lua_async_create_item(contp, ts_lua_flush_cleanup, (void *)action, ci);
  TSContDataSet(contp, ai);

  return 0;
}

static int
ts_lua_flush_wakeup_handler(TSCont contp, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;

  ai = TSContDataGet(contp);
  ci = ai->cinfo;

  ai->data = NULL;

  ts_lua_flush_cleanup(ai);

  TSContCall(ci->contp, TS_LUA_EVENT_COROUTINE_CONT, 0);

  return 0;
}

static int
ts_lua_flush_cleanup(ts_lua_async_item *ai)
{
  if (ai->deleted) {
    return 0;
  }

  if (ai->data) {
    TSActionCancel((TSAction)ai->data);
    ai->data = NULL;
  }

  TSContDestroy(ai->contp);
  ai->deleted = 1;

  return 0;
}
