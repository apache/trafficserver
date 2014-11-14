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

#define TS_LUA_FUNCTION_HTTP_INTERCEPT            "do_intercept"
#define TS_LUA_FUNCTION_HTTP_SERVER_INTERCEPT     "do_server_intercept"

typedef enum
{
  TS_LUA_TYPE_HTTP_INTERCEPT = 0,
  TS_LUA_TYPE_HTTP_SERVER_INTERCEPT = 1
} TSInterceptType;

static int ts_lua_http_intercept(lua_State * L);
static int ts_lua_http_server_intercept(lua_State * L);
static int ts_lua_http_intercept_entry(TSCont contp, TSEvent event, void *edata);
static void ts_lua_http_intercept_process(ts_lua_http_ctx * http_ctx, TSVConn conn);
static void ts_lua_http_intercept_setup_read(ts_lua_http_intercept_ctx * ictx);
static void ts_lua_http_intercept_setup_write(ts_lua_http_intercept_ctx * ictx);
static int ts_lua_http_intercept_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_http_intercept_run_coroutine(ts_lua_http_intercept_ctx * ictx, int n);
static int ts_lua_http_intercept_process_read(TSEvent event, ts_lua_http_intercept_ctx * ictx);
static int ts_lua_http_intercept_process_write(TSEvent event, ts_lua_http_intercept_ctx * ictx);

extern int ts_lua_flush_launch(ts_lua_http_intercept_ctx * ictx);


void
ts_lua_inject_http_intercept_api(lua_State * L)
{
  lua_pushcfunction(L, ts_lua_http_intercept);
  lua_setfield(L, -2, "intercept");

  lua_pushcfunction(L, ts_lua_http_server_intercept);
  lua_setfield(L, -2, "server_intercept");
}

static int
ts_lua_http_intercept(lua_State * L)
{
  TSCont contp;
  int type;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);
  http_ctx->has_hook = 1;

  type = lua_type(L, 1);

  if (type != LUA_TFUNCTION) {
    TSError("[%s] param in ts.http.intercept should be a function", __FUNCTION__);
    return 0;
  }

  lua_pushvalue(L, 1);
  lua_setglobal(L, TS_LUA_FUNCTION_HTTP_INTERCEPT);

  http_ctx->intercept_type = TS_LUA_TYPE_HTTP_INTERCEPT;

  contp = TSContCreate(ts_lua_http_intercept_entry, TSMutexCreate());
  TSContDataSet(contp, http_ctx);
  TSHttpTxnIntercept(contp, http_ctx->txnp);

  return 0;
}

static int
ts_lua_http_server_intercept(lua_State * L)
{
  TSCont contp;
  int type;
  ts_lua_http_ctx *http_ctx;

  http_ctx = ts_lua_get_http_ctx(L);
  http_ctx->has_hook = 1;

  type = lua_type(L, 1);

  if (type != LUA_TFUNCTION) {
    TSError("[%s] param in ts.http.server_intercept should be a function", __FUNCTION__);
    return 0;
  }

  lua_pushvalue(L, 1);
  lua_setglobal(L, TS_LUA_FUNCTION_HTTP_SERVER_INTERCEPT);

  http_ctx->intercept_type = TS_LUA_TYPE_HTTP_SERVER_INTERCEPT;

  contp = TSContCreate(ts_lua_http_intercept_entry, TSMutexCreate());
  TSContDataSet(contp, http_ctx);
  TSHttpTxnServerIntercept(contp, http_ctx->txnp);

  return 0;
}


static int
ts_lua_http_intercept_entry(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {

  case TS_EVENT_NET_ACCEPT_FAILED:
    if (edata)
      TSVConnClose((TSVConn) edata);
    break;

  case TS_EVENT_NET_ACCEPT:
    ts_lua_http_intercept_process((ts_lua_http_ctx *) TSContDataGet(contp), (TSVConn) edata);
    break;

  default:
    break;
  }

  TSContDestroy(contp);
  return 0;
}

static void
ts_lua_http_intercept_process(ts_lua_http_ctx * http_ctx, TSVConn conn)
{
  TSCont contp;
  lua_State *l;
  TSMutex mtxp;
  ts_lua_http_intercept_ctx *ictx;

  mtxp = http_ctx->mctx->mutexp;
  TSMutexLock(mtxp);

  ictx = ts_lua_create_http_intercept_ctx(http_ctx);

  contp = TSContCreate(ts_lua_http_intercept_handler, TSMutexCreate());
  TSContDataSet(contp, ictx);

  ictx->contp = contp;
  ictx->net_vc = conn;

  l = ictx->lua;

  // set up read.
  ts_lua_http_intercept_setup_read(ictx);

  // set up write.
  ts_lua_http_intercept_setup_write(ictx);

  // invoke function here
  if (http_ctx->intercept_type == TS_LUA_TYPE_HTTP_INTERCEPT) {
    lua_getglobal(l, TS_LUA_FUNCTION_HTTP_INTERCEPT);
  } else {
    lua_getglobal(l, TS_LUA_FUNCTION_HTTP_SERVER_INTERCEPT);
  }

  ts_lua_http_intercept_run_coroutine(ictx, 0);

  TSMutexUnlock(mtxp);

}

static void
ts_lua_http_intercept_setup_read(ts_lua_http_intercept_ctx * ictx)
{
  ictx->input.buffer = TSIOBufferCreate();
  ictx->input.reader = TSIOBufferReaderAlloc(ictx->input.buffer);
  ictx->input.vio = TSVConnRead(ictx->net_vc, ictx->contp, ictx->input.buffer, INT64_MAX);
}

static void
ts_lua_http_intercept_setup_write(ts_lua_http_intercept_ctx * ictx)
{
  ictx->output.buffer = TSIOBufferCreate();
  ictx->output.reader = TSIOBufferReaderAlloc(ictx->output.buffer);
  ictx->output.vio = TSVConnWrite(ictx->net_vc, ictx->contp, ictx->output.reader, INT64_MAX);
}

static int
ts_lua_http_intercept_handler(TSCont contp, TSEvent event, void *edata)
{
  int ret, n;
  TSMutex mtxp;
  ts_lua_http_intercept_ctx *ictx;

  ictx = (ts_lua_http_intercept_ctx *) TSContDataGet(contp);
  mtxp = NULL;

  if (edata == ictx->input.vio) {
    ret = ts_lua_http_intercept_process_read(event, ictx);

  } else if (edata == ictx->output.vio) {
    ret = ts_lua_http_intercept_process_write(event, ictx);

  } else {
    mtxp = ictx->mctx->mutexp;
    n = (int64_t) edata & 0xFFFF;
    TSMutexLock(mtxp);
    ret = ts_lua_http_intercept_run_coroutine(ictx, n);
  }

  if (ret || (ictx->send_complete && ictx->recv_complete)) {

    TSContDestroy(contp);

    if (!mtxp) {
      mtxp = ictx->mctx->mutexp;
      TSMutexLock(mtxp);
    }

    ts_lua_destroy_http_intercept_ctx(ictx);
  }

  if (mtxp)
    TSMutexUnlock(mtxp);

  return 0;
}

static int
ts_lua_http_intercept_run_coroutine(ts_lua_http_intercept_ctx * ictx, int n)
{
  int ret;
  int64_t avail;
  int64_t done;
  lua_State *L;

  L = ictx->lua;

  ret = lua_resume(L, n);

  switch (ret) {

  case 0:                      // finished
    avail = TSIOBufferReaderAvail(ictx->output.reader);
    done = TSVIONDoneGet(ictx->output.vio);
    TSVIONBytesSet(ictx->output.vio, avail + done);
    ictx->all_ready = 1;

    if (avail) {
      TSVIOReenable(ictx->output.vio);

    } else {
      ictx->send_complete = 1;
    }
    break;

  case 1:                      // yield
    break;

  default:                     // error
    TSError("lua_resume failed: %s", lua_tostring(L, -1));
    return -1;
  }

  return 0;
}

static int
ts_lua_http_intercept_process_read(TSEvent event, ts_lua_http_intercept_ctx * ictx)
{
  int64_t avail = TSIOBufferReaderAvail(ictx->input.reader);
  TSIOBufferReaderConsume(ictx->input.reader, avail);

  switch (event) {

  case TS_EVENT_VCONN_READ_READY:
    TSVConnShutdown(ictx->net_vc, 1, 0);

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
ts_lua_http_intercept_process_write(TSEvent event, ts_lua_http_intercept_ctx * ictx)
{
  int64_t done, avail;

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:

    avail = TSIOBufferReaderAvail(ictx->output.reader);

    if (ictx->all_ready) {
      TSVIOReenable(ictx->output.vio);

    } else if (ictx->to_flush > 0) {    // ts.flush()

      done = TSVIONDoneGet(ictx->output.vio);

      if (ictx->to_flush > done) {
        TSVIOReenable(ictx->output.vio);

      } else {                  // we had flush all the data we want
        ictx->to_flush = 0;
        ts_lua_flush_launch(ictx);      // wake up
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
