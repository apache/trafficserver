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
#include "ts_lua_io.h"
#include "ts_lua_fetch.h"

#define TS_LUA_EVENT_FETCH_OVER 20010
#define TS_LUA_FETCH_CLIENT_ADDRPORT "127.0.0.1:33333"
#define TS_LUA_FETCH_CLIENT_ADDRPORT_LEN 15
#define TS_LUA_FETCH_USER_AGENT "TS Fetcher/1.0"

static int ts_lua_fetch(lua_State *L);
static int ts_lua_fetch_multi(lua_State *L);
static int ts_lua_fetch_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_fetch_multi_cleanup(ts_lua_async_item *ai);
static int ts_lua_fetch_multi_handler(TSCont contp, TSEvent event, void *edata);
static int ts_lua_fetch_one_item(lua_State *L, const char *url, size_t url_len, ts_lua_fetch_info *fi);
static inline void ts_lua_destroy_fetch_multi_info(ts_lua_fetch_multi_info *fmi);

void
ts_lua_inject_fetch_api(lua_State *L)
{
  /* ts.fetch() */
  lua_pushcfunction(L, ts_lua_fetch);
  lua_setfield(L, -2, "fetch");

  /* ts.fetch_multi() */
  lua_pushcfunction(L, ts_lua_fetch_multi);
  lua_setfield(L, -2, "fetch_multi");
}

static int
ts_lua_fetch(lua_State *L)
{
  int sz;
  size_t n;
  const char *url;
  size_t url_len;
  TSCont contp;
  ts_lua_cont_info *ci;
  ts_lua_async_item *ai;
  ts_lua_fetch_info *fi;
  ts_lua_fetch_multi_info *fmi;

  ci = ts_lua_get_cont_info(L);
  if (ci == NULL) {
    return 0;
  }

  n = lua_gettop(L);
  if (n < 1) {
    return luaL_error(L, "'ts.fetch' requires parameter");
  }

  /* url */
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "'ts.fetch' first param is not string");
  }

  url = luaL_checklstring(L, 1, &url_len);

  /* replicate misc table */
  if (n >= 2) {
    lua_pushvalue(L, 2);

  } else {
    lua_pushnil(L);
  }

  contp = TSContCreate(ts_lua_fetch_multi_handler, ci->mutex);

  sz  = sizeof(ts_lua_fetch_multi_info) + 1 * sizeof(ts_lua_fetch_info);
  fmi = (ts_lua_fetch_multi_info *)TSmalloc(sz);

  memset(fmi, 0, sz);
  fmi->total = 1;
  fmi->contp = contp;

  fi         = &fmi->fiv[0];
  fi->fmi    = fmi;
  fi->buffer = TSIOBufferCreate();
  fi->reader = TSIOBufferReaderAlloc(fi->buffer);

  ts_lua_fetch_one_item(L, url, url_len, fi);

  // pop the replicated misc table
  lua_pop(L, 1);

  ai = ts_lua_async_create_item(contp, ts_lua_fetch_multi_cleanup, fmi, ci);
  TSContDataSet(contp, ai);

  return lua_yield(L, 0);
  ;
}

static int
ts_lua_fetch_multi(lua_State *L)
{
  int type, sz;
  size_t i, n;
  const char *url;
  size_t url_len;
  TSCont contp;
  ts_lua_cont_info *ci;
  ts_lua_async_item *ai;
  ts_lua_fetch_info *fi;
  ts_lua_fetch_multi_info *fmi;

  ci = ts_lua_get_cont_info(L);
  if (ci == NULL) {
    return 0;
  }

  if (lua_gettop(L) < 1) {
    return luaL_error(L, "'ts.fetch_mutli' requires one parameter");
  }

  type = lua_type(L, 1);
  if (type != LUA_TTABLE) {
    return luaL_error(L, "'ts.fetch_mutli' requires table as parameter");
  }

  // main continuation handler
  contp = TSContCreate(ts_lua_fetch_multi_handler, ci->mutex);

  // Iterate the table
  n = lua_objlen(L, 1);

  sz  = sizeof(ts_lua_fetch_multi_info) + n * sizeof(ts_lua_fetch_info);
  fmi = (ts_lua_fetch_multi_info *)TSmalloc(sz);

  memset(fmi, 0, sz);
  fmi->total = n;
  fmi->contp = contp;
  fmi->multi = 1;

  for (i = 0; i < n; i++) {
    /* push fetch item */
    lua_pushinteger(L, i + 1);
    lua_gettable(L, -2);

    if (lua_objlen(L, -1) < 1) {
      ts_lua_destroy_fetch_multi_info(fmi);
      TSContDestroy(contp);

      return luaL_error(L, "'ts.fetch_mutli' got empty table item");
    }

    /* push url */
    lua_pushnumber(L, 1);
    lua_gettable(L, -2);

    if (!lua_isstring(L, -1)) {
      ts_lua_destroy_fetch_multi_info(fmi);
      TSContDestroy(contp);

      return luaL_error(L, "'ts.fetch_mutli' got invalid table item: url illegal");
    }

    url = luaL_checklstring(L, -1, &url_len);

    /* push misc table */
    lua_pushinteger(L, 2);
    lua_gettable(L, -3);

    fi         = &fmi->fiv[i];
    fi->fmi    = fmi;
    fi->buffer = TSIOBufferCreate();
    fi->reader = TSIOBufferReaderAlloc(fi->buffer);

    ts_lua_fetch_one_item(L, url, url_len, fi);
    lua_pop(L, 3); // misc table, url, fetch item
  }

  ai = ts_lua_async_create_item(contp, ts_lua_fetch_multi_cleanup, (void *)fmi, ci);
  TSContDataSet(contp, ai);

  return lua_yield(L, 0);
  ;
}

static int
ts_lua_fetch_one_item(lua_State *L, const char *url, size_t url_len, ts_lua_fetch_info *fi)
{
  TSCont contp;
  int tb, flags, host_len, n;
  int cl, ht, ua;
  const char *method, *key, *value, *body, *opt;
  const char *addr, *ptr, *host;
  size_t method_len, key_len, value_len, body_len;
  size_t addr_len, opt_len, i, left;
  char c;
  struct sockaddr clientaddr;
  char buf[32];

  tb = lua_istable(L, -1);

  /* method */
  if (tb) {
    lua_pushlstring(L, "method", sizeof("method") - 1);
    lua_gettable(L, -2);
    if (lua_isstring(L, -1)) {
      method = luaL_checklstring(L, -1, &method_len);

    } else {
      method     = "GET";
      method_len = sizeof("GET") - 1;
    }

    lua_pop(L, 1);

  } else {
    method     = "GET";
    method_len = sizeof("GET") - 1;
  }

  /* body */
  body     = NULL;
  body_len = 0;

  if (tb) {
    lua_pushlstring(L, "body", sizeof("body") - 1);
    lua_gettable(L, -2);

    if (lua_isstring(L, -1)) {
      body = luaL_checklstring(L, -1, &body_len);
    }

    lua_pop(L, 1);
  }

  /* cliaddr */
  memset(&clientaddr, 0, sizeof(clientaddr));

  if (tb) {
    lua_pushlstring(L, "cliaddr", sizeof("cliaddr") - 1);
    lua_gettable(L, -2);

    if (lua_isstring(L, -1)) {
      addr = luaL_checklstring(L, -1, &addr_len);

      if (TS_ERROR == TSIpStringToAddr(addr, addr_len, &clientaddr)) {
        TSError("[%s] Client ip parse failed! Using default.", TS_LUA_DEBUG_TAG);
        if (TS_ERROR == TSIpStringToAddr(TS_LUA_FETCH_CLIENT_ADDRPORT, TS_LUA_FETCH_CLIENT_ADDRPORT_LEN, &clientaddr)) {
          TSError("[%s] Default client ip parse failed!", TS_LUA_DEBUG_TAG);
          return 0;
        }
      }
    }

    lua_pop(L, 1);
  }

  /* option */
  flags = TS_FETCH_FLAGS_DECHUNK; // dechunk the body default

  if (tb) {
    lua_pushlstring(L, "option", sizeof("option") - 1);
    lua_gettable(L, -2);

    if (lua_isstring(L, -1)) {
      opt = luaL_checklstring(L, -1, &opt_len);

      for (i = 0; i < opt_len; i++) {
        c = opt[i];

        switch (c) {
        case 'c':
          flags &= (~TS_FETCH_FLAGS_DECHUNK);
          break;

        default:
          break;
        }
      }
    }

    lua_pop(L, 1);
  }

  contp = TSContCreate(ts_lua_fetch_handler, TSContMutexGet(fi->fmi->contp)); // reuse parent cont's mutex
  TSContDataSet(contp, fi);

  fi->contp = contp;
  fi->fch   = TSFetchCreate(contp, method, url, "HTTP/1.1", (struct sockaddr *)&clientaddr, flags);

  /* header */
  cl = ht = ua = 0;

  if (tb) {
    lua_pushlstring(L, "header", sizeof("header") - 1);
    lua_gettable(L, -2);

    if (lua_istable(L, -1)) {
      // iterate the header table
      lua_pushnil(L);

      while (lua_next(L, -2)) {
        lua_pushvalue(L, -2);

        key   = luaL_checklstring(L, -1, &key_len);
        value = luaL_checklstring(L, -2, &value_len);

        if ((int)key_len == TS_MIME_LEN_CONTENT_LENGTH &&
            !strncasecmp(TS_MIME_FIELD_CONTENT_LENGTH, key, key_len)) { // Content-Length
          cl = 1;

        } else if ((int)key_len == TS_MIME_LEN_HOST && !strncasecmp(TS_MIME_FIELD_HOST, key, key_len)) { // Host
          ht = 1;

        } else if ((int)key_len == TS_MIME_LEN_USER_AGENT && !strncasecmp(TS_MIME_FIELD_USER_AGENT, key, key_len)) { // User-Agent
          ua = 1;
        }

        TSFetchHeaderAdd(fi->fch, key, key_len, value, value_len);

        lua_pop(L, 2);
      }
    }

    lua_pop(L, 1);
  }

  /* Host */
  if (ht == 0) {
    ptr = memchr(url, ':', url_len);

    if (ptr) {
      host = ptr + 3;
      left = url_len - (host - url);

      ptr = memchr(host, '/', left);

      if (ptr) {
        host_len = ptr - host;

      } else {
        host_len = left;
      }

      TSFetchHeaderAdd(fi->fch, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, host, host_len);
    }
  }

  /* User-Agent */
  if (ua == 0) {
    TSFetchHeaderAdd(fi->fch, TS_MIME_FIELD_USER_AGENT, TS_MIME_LEN_USER_AGENT, TS_LUA_FETCH_USER_AGENT,
                     sizeof(TS_LUA_FETCH_USER_AGENT) - 1);
  }

  if (body_len > 0 && cl == 0) { // add Content-Length header
    n = sprintf(buf, "%zu", body_len);
    TSFetchHeaderAdd(fi->fch, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, buf, n);
  }

  TSFetchLaunch(fi->fch);

  if (body_len > 0) {
    TSFetchWriteData(fi->fch, body, body_len);
  }

  return 0;
}

static int
ts_lua_fetch_handler(TSCont contp, TSEvent ev, void *edata ATS_UNUSED)
{
  int event;
  char *from;
  int64_t n, wavail;
  TSIOBufferBlock blk;

  ts_lua_fetch_info *fi;
  ts_lua_fetch_multi_info *fmi;

  event = (int)ev;
  fi    = TSContDataGet(contp);
  fmi   = fi->fmi;

  switch (event) {
  case TS_FETCH_EVENT_EXT_HEAD_READY:
  case TS_FETCH_EVENT_EXT_HEAD_DONE:
    break;

  case TS_FETCH_EVENT_EXT_BODY_READY:
  case TS_FETCH_EVENT_EXT_BODY_DONE:

    do {
      blk  = TSIOBufferStart(fi->buffer);
      from = TSIOBufferBlockWriteStart(blk, &wavail);
      n    = TSFetchReadData(fi->fch, from, wavail);
      TSIOBufferProduce(fi->buffer, n);
    } while (n == wavail);

    if (event == TS_FETCH_EVENT_EXT_BODY_DONE) { // fetch over
      fi->over = 1;
    }

    break;

  default:
    fi->failed = 1;
    break;
  }

  if (fmi && (fi->over || fi->failed)) {
    TSContCall(fmi->contp, TS_LUA_EVENT_FETCH_OVER, fi); // error exist
    ts_lua_destroy_fetch_multi_info(fmi);
  }

  return 0;
}

static int
ts_lua_fill_one_result(lua_State *L, ts_lua_fetch_info *fi)
{
  const char *name, *value;
  int name_len, value_len;
  char *dst;
  int64_t ravail;
  TSMBuffer bufp;
  TSMLoc hdrp;
  TSMLoc field_loc, next_field_loc;
  TSHttpStatus status;

  bufp = TSFetchRespHdrMBufGet(fi->fch);
  hdrp = TSFetchRespHdrMLocGet(fi->fch);

  // result table
  lua_newtable(L);

  // status code
  status = TSHttpHdrStatusGet(bufp, hdrp);
  lua_pushlstring(L, "status", sizeof("status") - 1);
  lua_pushnumber(L, status);
  lua_rawset(L, -3);

  // header
  lua_pushlstring(L, "header", sizeof("header") - 1);
  lua_newtable(L);

  field_loc = TSMimeHdrFieldGet(bufp, hdrp, 0);
  while (field_loc) {
    name  = TSMimeHdrFieldNameGet(bufp, hdrp, field_loc, &name_len);
    value = TSMimeHdrFieldValueStringGet(bufp, hdrp, field_loc, -1, &value_len);

    lua_pushlstring(L, name, name_len);
    lua_pushlstring(L, value, value_len);
    lua_rawset(L, -3);

    next_field_loc = TSMimeHdrFieldNext(bufp, hdrp, field_loc);
    TSHandleMLocRelease(bufp, hdrp, field_loc);
    field_loc = next_field_loc;
  }
  lua_rawset(L, -3);

  // body
  ravail = TSIOBufferReaderAvail(fi->reader);
  if (ravail > 0) {
    lua_pushlstring(L, "body", sizeof("body") - 1);

    dst = (char *)TSmalloc(ravail);
    IOBufferReaderCopy(fi->reader, dst, ravail);
    lua_pushlstring(L, (char *)dst, ravail);

    lua_rawset(L, -3);
    TSfree(dst);
  }

  // truncated
  lua_pushlstring(L, "truncated", sizeof("truncated") - 1);
  if (fi->failed) {
    lua_pushboolean(L, 1);

  } else {
    lua_pushboolean(L, 0);
  }

  lua_rawset(L, -3);

  return 0;
}

static int
ts_lua_fetch_multi_handler(TSCont contp, TSEvent event ATS_UNUSED, void *edata)
{
  int i;
  lua_State *L;
  TSMutex lmutex;

  ts_lua_async_item *ai;
  ts_lua_cont_info *ci;
  ts_lua_fetch_info *fi;
  ts_lua_fetch_multi_info *fmi;

  ai = TSContDataGet(contp);
  ci = ai->cinfo;

  fmi = (ts_lua_fetch_multi_info *)ai->data;
  fi  = (ts_lua_fetch_info *)edata;

  L      = ai->cinfo->routine.lua;
  lmutex = ai->cinfo->routine.mctx->mutexp;

  fmi->done++;

  if (fi->fmi != fmi && fmi->done != fmi->total) {
    return 0;
  }

  // all finish
  TSMutexLock(lmutex);

  if (fmi->total == 1 && !fmi->multi) {
    ts_lua_fill_one_result(L, fi);
    TSContCall(ci->contp, TS_LUA_EVENT_COROUTINE_CONT, (void *)1);

  } else {
    lua_newtable(L);

    for (i = 1; i <= fmi->total; i++) {
      ts_lua_fill_one_result(L, &fmi->fiv[i - 1]);
      lua_rawseti(L, -2, i);
    }

    TSContCall(ci->contp, TS_LUA_EVENT_COROUTINE_CONT, (void *)1);
  }

  TSMutexUnlock(lmutex);
  return 0;
}

static inline void
ts_lua_destroy_fetch_multi_info(ts_lua_fetch_multi_info *fmi)
{
  int i;
  ts_lua_fetch_info *fi;

  if (fmi == NULL) {
    return;
  }

  for (i = 0; i < fmi->total; i++) {
    fi = &fmi->fiv[i];

    if (fi->reader) {
      TSIOBufferReaderFree(fi->reader);
    }

    if (fi->buffer) {
      TSIOBufferDestroy(fi->buffer);
    }

    if (fi->fch) {
      TSFetchDestroy(fi->fch);
    }

    if (fi->contp) {
      TSContDestroy(fi->contp);
    }
  }

  TSfree(fmi);
}

static int
ts_lua_fetch_multi_cleanup(ts_lua_async_item *ai)
{
  if (ai->deleted) {
    return 0;
  }

  if (ai->data) {
    ai->data = NULL;
    TSContDestroy(ai->contp);
    ai->contp = NULL;
  }

  ai->deleted = 1;

  return 0;
}
