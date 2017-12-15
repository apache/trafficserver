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

static int ts_lua_transform_handler(TSCont contp, ts_lua_http_transform_ctx *transform_ctx, TSEvent event, int n);

int
ts_lua_transform_entry(TSCont contp, TSEvent ev, void *edata)
{
  int n, event;
  TSVIO input_vio;
  ts_lua_http_transform_ctx *transform_ctx;

  event         = (int)ev;
  transform_ctx = (ts_lua_http_transform_ctx *)TSContDataGet(contp);

  if (TSVConnClosedGet(contp)) {
    ts_lua_destroy_http_transform_ctx(transform_ctx);
    return 0;
  }

  n = 0;

  switch (event) {
  case TS_EVENT_ERROR:
    input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_LUA_EVENT_COROUTINE_CONT:
    n = (intptr_t)edata;
  /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_READY:
  default:
    ts_lua_transform_handler(contp, transform_ctx, event, n);
    break;
  }

  return 0;
}

static int
ts_lua_transform_handler(TSCont contp, ts_lua_http_transform_ctx *transform_ctx, TSEvent event, int n)
{
  TSVConn output_conn;
  TSVIO input_vio;
  TSIOBufferReader input_reader;
  TSIOBufferBlock blk;
  int64_t toread, towrite, blk_len, upstream_done, input_avail, l;
  const char *start;
  const char *res;
  size_t res_len;
  int ret, eos, write_down, rc, top, empty_input;
  ts_lua_coroutine *crt;
  ts_lua_cont_info *ci;

  lua_State *L;
  TSMutex mtxp;

  ci  = &transform_ctx->cinfo;
  crt = &ci->routine;

  mtxp = crt->mctx->mutexp;
  L    = crt->lua;

  output_conn = TSTransformOutputVConnGet(contp);
  input_vio   = TSVConnWriteVIOGet(contp);

  empty_input = 0;
  if (!TSVIOBufferGet(input_vio)) {
    if (transform_ctx->output.vio) {
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] reenabling ouput VIO after input VIO does not exist", __FUNCTION__);
      TSVIONBytesSet(transform_ctx->output.vio, transform_ctx->total);
      TSVIOReenable(transform_ctx->output.vio);
      return 0;
    } else {
      TSDebug(TS_LUA_DEBUG_TAG, "[%s] no input VIO and output VIO", __FUNCTION__);
      empty_input = 1;
    }
  }

  if (empty_input == 0) {
    input_reader = TSVIOReaderGet(input_vio);
  }

  if (!transform_ctx->output.buffer) {
    transform_ctx->output.buffer = TSIOBufferCreate();
    transform_ctx->output.reader = TSIOBufferReaderAlloc(transform_ctx->output.buffer);

    transform_ctx->reserved.buffer = TSIOBufferCreate();
    transform_ctx->reserved.reader = TSIOBufferReaderAlloc(transform_ctx->reserved.buffer);

    if (empty_input == 0) {
      transform_ctx->upstream_bytes = TSVIONBytesGet(input_vio);
    } else {
      transform_ctx->upstream_bytes = 0;
    }

    transform_ctx->downstream_bytes = INT64_MAX;
  }

  if (empty_input == 0) {
    input_avail   = TSIOBufferReaderAvail(input_reader);
    upstream_done = TSVIONDoneGet(input_vio);
    toread        = TSVIONTodoGet(input_vio);

    if (toread <= input_avail) { // upstream finished
      eos = 1;
    } else {
      eos = 0;
    }
  } else {
    input_avail = 0;
    toread      = 0;
    eos         = 1;
  }

  if (input_avail > 0) {
    // move to the reserved.buffer
    TSIOBufferCopy(transform_ctx->reserved.buffer, input_reader, input_avail, 0);

    // reset input
    TSIOBufferReaderConsume(input_reader, input_avail);
    TSVIONDoneSet(input_vio, upstream_done + input_avail);
  }

  write_down = 0;
  if (empty_input == 0) {
    towrite = TSIOBufferReaderAvail(transform_ctx->reserved.reader);
  } else {
    towrite = 0;
  }

  TSMutexLock(mtxp);
  ts_lua_set_cont_info(L, ci);

  do {
    if (event == TS_LUA_EVENT_COROUTINE_CONT) {
      event = 0;
      goto launch;
    } else {
      n = 2;
    }

    if (towrite == 0 && empty_input == 0) {
      break;
    }

    if (empty_input == 0) {
      blk   = TSIOBufferReaderStart(transform_ctx->reserved.reader);
      start = TSIOBufferBlockReadStart(blk, transform_ctx->reserved.reader, &blk_len);

      lua_pushlightuserdata(L, transform_ctx);
      lua_rawget(L, LUA_GLOBALSINDEX); /* push function */

      if (towrite > blk_len) {
        lua_pushlstring(L, start, (size_t)blk_len);
        towrite -= blk_len;
        TSIOBufferReaderConsume(transform_ctx->reserved.reader, blk_len);
      } else {
        lua_pushlstring(L, start, (size_t)towrite);
        TSIOBufferReaderConsume(transform_ctx->reserved.reader, towrite);
        towrite = 0;
      }

      if (!towrite && eos) {
        lua_pushinteger(L, 1); /* second param, data finished */
      } else {
        lua_pushinteger(L, 0); /* second param, data not finish */
      }
    } else {
      lua_pushlightuserdata(L, transform_ctx);
      lua_rawget(L, LUA_GLOBALSINDEX); /* push function */

      lua_pushlstring(L, "", 0);
      lua_pushinteger(L, 1); /* second param, data finished */
    }

  launch:
    rc  = lua_resume(L, n);
    top = lua_gettop(L);

    switch (rc) {
    case LUA_YIELD: // coroutine yield
      TSMutexUnlock(mtxp);
      return 0;

    case 0: // coroutine success
      if (top == 2) {
        ret = lua_tointeger(L, -1); /* 0 is not finished, 1 is finished */
        res = lua_tolstring(L, -2, &res_len);
      } else { // what hells code are you writing ?
        ret     = 0;
        res     = NULL;
        res_len = 0;
      }
      break;

    default: // coroutine failed
      TSError("[ts_lua] lua_resume failed: %s", lua_tostring(L, -1));
      ret     = 1;
      res     = NULL;
      res_len = 0;
      break;
    }

    if (res && res_len > 0) {
      if (!transform_ctx->output.vio) {
        l = transform_ctx->downstream_bytes;
        if (ret) {
          l = res_len;
        }

        transform_ctx->output.vio = TSVConnWrite(output_conn, contp, transform_ctx->output.reader, l); // HttpSM go on
      }

      TSIOBufferWrite(transform_ctx->output.buffer, res, res_len);
      transform_ctx->total += res_len;
      write_down = 1;
    }

    lua_pop(L, top);

    if (ret || (eos && !towrite)) { // EOS
      eos = 1;
      break;
    }

  } while (towrite > 0);

  TSMutexUnlock(mtxp);

  if (eos && !transform_ctx->output.vio) {
    transform_ctx->output.vio = TSVConnWrite(output_conn, contp, transform_ctx->output.reader, 0);
  }

  if (write_down || eos) {
    TSVIOReenable(transform_ctx->output.vio);
  }

  if (toread > input_avail) { // upstream not finished.
    if (eos) {
      TSVIONBytesSet(transform_ctx->output.vio, transform_ctx->total);
      if (empty_input == 0) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_EOS, input_vio);
      }
    } else {
      if (empty_input == 0) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
      }
    }
  } else { // upstream is finished.
    TSVIONBytesSet(transform_ctx->output.vio, transform_ctx->total);
    if (empty_input == 0) {
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
    }
  }

  return 0;
}
