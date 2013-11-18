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

#include "ts/ts.h"
#include "ts/remap.h"
#include "ink_defs.h"

#include <string.h>
#include "lapi.h"
#include "lutil.h"
#include "hook.h"

template <typename LuaType, typename Param1> LuaType *
push_userdata_object(lua_State * lua, Param1 p1)
{
  LuaType * ltype;
  ltype = LuaType::alloc(lua, p1);
  TSReleaseAssert(lua_isuserdata(lua, -1) == 1);
  return ltype;
}

template <typename LuaType, typename Param1, typename Param2> LuaType *
push_userdata_object(lua_State * lua, Param1 p1, Param2 p2)
{
  LuaType * ltype;
  ltype = LuaType::alloc(lua, p1, p2);
  TSReleaseAssert(lua_isuserdata(lua, -1) == 1);
  return ltype;
}

struct LuaRemapHeaders
{
  TSMBuffer buffer;
  TSMLoc    headers;

  static LuaRemapHeaders * get(lua_State * lua, int index) {
    return (LuaRemapHeaders *)luaL_checkudata(lua, index, "ts.meta.rri.headers");
  }

  static LuaRemapHeaders * alloc(lua_State * lua) {
    LuaRemapHeaders * hdrs;

    hdrs = LuaNewUserData<LuaRemapHeaders>(lua);
    luaL_getmetatable(lua, "ts.meta.rri.headers");
    lua_setmetatable(lua, -2);

    return hdrs;
  }
};

struct LuaHttpTransaction
{
  TSHttpTxn txn;

  LuaHttpTransaction() : txn(NULL) {}

  static LuaHttpTransaction * get(lua_State * lua, int index) {
    return (LuaHttpTransaction *)luaL_checkudata(lua, index, "ts.meta.http.txn");
  }

  static LuaHttpTransaction * alloc(lua_State * lua, TSHttpTxn ptr) {
    LuaHttpTransaction * txn;

    txn = LuaNewUserData<LuaHttpTransaction>(lua);
    txn->txn = ptr;
    luaL_getmetatable(lua, "ts.meta.http.txn");
    lua_setmetatable(lua, -2);

    return txn;
  }
};

struct LuaHttpSession
{
  TSHttpSsn ssn;      // session pointer

  LuaHttpSession() : ssn(NULL) {}

  static LuaHttpSession * get(lua_State * lua, int index) {
    return (LuaHttpSession *)luaL_checkudata(lua, index, "ts.meta.http.ssn");
  }

  static LuaHttpSession * alloc(lua_State * lua, TSHttpSsn ptr) {
    LuaHttpSession * ssn;

    ssn = LuaNewUserData<LuaHttpSession>(lua);
    ssn->ssn = ptr;
    luaL_getmetatable(lua, "ts.meta.http.ssn");
    lua_setmetatable(lua, -2);

    return ssn;
  }
};

LuaRemapRequest *
LuaRemapRequest::get(lua_State * lua, int index)
{
  return (LuaRemapRequest *)luaL_checkudata(lua, index, "ts.meta.rri");
}

LuaRemapRequest *
LuaRemapRequest::alloc(lua_State * lua, TSRemapRequestInfo * rri, TSHttpTxn txn)
{
  LuaRemapRequest * rq;

  rq = new(lua_newuserdata(lua, sizeof(LuaRemapRequest))) LuaRemapRequest(rri, txn);
  luaL_getmetatable(lua, "ts.meta.rri");
  lua_setmetatable(lua, -2);

  // Stash a new table as the environment for this object. We will use it later for __index.
  lua_newtable(lua);
  TSReleaseAssert(lua_setfenv(lua, -2));

  return rq;
}

// Given a URL table on the top of the stack, pop it's values into the URL buffer.
bool
LuaPopUrl(lua_State * lua, TSMBuffer buffer, TSMLoc url)
{
  const char *  strval;
  size_t        len;

#define SET_URL_COMPONENT(name, setter) do { \
  lua_getfield(lua, -1, name); \
  if (!lua_isnil(lua, -1)) { \
    strval = luaL_checklstring(lua, -1, &len); \
    if (strval) { \
      setter(buffer, url, strval, len); \
    } \
  } \
  lua_pop(lua, 1); \
} while (0)

  // We ignore the 'href' field. When constructing URL tables, it's convenient, but it doesn't seem
  // necessary here. Callers can easily construct the URL table.
  SET_URL_COMPONENT("scheme", TSUrlSchemeSet);
  SET_URL_COMPONENT("user", TSUrlUserSet);
  SET_URL_COMPONENT("password", TSUrlPasswordSet);
  SET_URL_COMPONENT("host", TSUrlHostSet);
  SET_URL_COMPONENT("path", TSUrlPathSet);
  SET_URL_COMPONENT("query", TSUrlHttpQuerySet);
  SET_URL_COMPONENT("fragment", TSUrlHttpFragmentSet);

  lua_getfield(lua, -1, "port");
  if (!lua_isnil(lua, -1)) {
    TSUrlPortSet(buffer, url, luaL_checkint(lua, -1));
  }
  lua_pop(lua, 1);

#undef SET_URL_COMPONENT
  return true;
}

bool
LuaPushUrl(lua_State * lua, TSMBuffer buffer, TSMLoc url)
{
  int len;
  const char * str;

#define PUSH_URL_COMPONENT(accessor, name) do { \
  str = accessor(buffer, url, &len); \
  if (str) { \
    lua_pushlstring(lua, str, len); \
  }  else { \
    lua_pushnil(lua); \
  } \
  lua_setfield(lua, -2, name); \
} while (0)

  lua_newtable(lua);

  // Set fundamental URL fields.
  // XXX should we be luvit-compatible with these names?
  PUSH_URL_COMPONENT(TSUrlSchemeGet, "scheme");       // luvit: protocol
  PUSH_URL_COMPONENT(TSUrlUserGet, "user");
  PUSH_URL_COMPONENT(TSUrlPasswordGet, "password");
  PUSH_URL_COMPONENT(TSUrlHostGet, "host");
  lua_pushinteger(lua, TSUrlPortGet(buffer, url));
  lua_setfield(lua, -2, "port");
  PUSH_URL_COMPONENT(TSUrlPathGet, "path");           // luvit: pathname
  PUSH_URL_COMPONENT(TSUrlHttpQueryGet, "query");     // luvit: search
  PUSH_URL_COMPONENT(TSUrlHttpFragmentGet, "fragment");

  // It would be cleaner to add a __tostring metamethod, but to do that we would have to keep the
  // buffer and url around indefinitely. Better to make a straight copy now; use the 'href' key
  // just like luvit does.
  str = TSUrlStringGet(buffer, url, &len);
  if (str) {
    lua_pushlstring(lua, str, len);
    lua_setfield(lua, -2, "href");
    TSfree((void *)str);
  }

  TSReleaseAssert(lua_istable(lua, -1) == 1);
  return true;

#undef PUSH_URL_COMPONENT
}

static int
LuaRemapRedirect(lua_State * lua)
{
  LuaRemapRequest * rq;

  rq = LuaRemapRequest::get(lua, 1);
  luaL_checktype(lua, 2, LUA_TTABLE);

  LuaLogDebug("redirecting request %p", rq->rri);

  lua_pushvalue(lua, 2);
  LuaPopUrl(lua, rq->rri->requestBufp, rq->rri->requestUrl);
  lua_pop(lua, 1);

  // A redirect always terminates plugin chain evaluation.
  rq->rri->redirect = 1;
  rq->status = TSREMAP_DID_REMAP_STOP;

  // Return true back to Lua-space.
  lua_pushboolean(lua, 1);
  return 1;
}

static int
LuaRemapRewrite(lua_State * lua)
{
  LuaRemapRequest * rq;

  rq = LuaRemapRequest::get(lua, 1);
  luaL_checktype(lua, 2, LUA_TTABLE);

  LuaLogDebug("rewriting request %p", rq->rri);

  lua_pushvalue(lua, 2);
  LuaPopUrl(lua, rq->rri->requestBufp, rq->rri->requestUrl);
  lua_pop(lua, 1);

  // A rewrite updates the request URL but never terminates plugin chain evaluation.
  rq->status = TSREMAP_DID_REMAP;

  // Return true back to Lua-space.
  lua_pushboolean(lua, 1);
  return 1;
}

static int
LuaRemapReject(lua_State * lua)
{
  LuaRemapRequest * rq;
  int status;
  const char * body = NULL;

  rq = LuaRemapRequest::get(lua, 1);
  status = luaL_checkint(lua, 2);
  if (!lua_isnoneornil(lua, 3)) {
    body = luaL_checkstring(lua, 3);
  }

  LuaLogDebug("rejecting request %p with status %d", rq->rri, status);

  TSHttpTxnSetHttpRetStatus(rq->txn, (TSHttpStatus)status);
  if (body && *body) {
    // XXX Dubiously guess the content type from the body. This doesn't actually seem to work
    // so it doesn't matter that our guess is pretty bad.
    int isplain = (*body != '<');
    TSHttpTxnSetHttpRetBody(rq->txn, body, isplain);
  }

  // A reject terminates plugin chain evaluation but does not update the request URL.
  rq->status = TSREMAP_NO_REMAP_STOP;

  return 1;
}

static int
LuaRemapUrl(lua_State * lua)
{
  LuaRemapRequest * rq;

  rq = LuaRemapRequest::get(lua, 1);
  LuaPushUrl(lua, rq->rri->requestBufp, rq->rri->requestUrl);
  return 1;
}

// Since we cannot add fields to userdata objects, we use the environment to store the fields. If the requested
// field isn't in our metatable, try to find it in the environment. Populate keys in the environment on demand if
// the request is for a key that we know about.
//
// XXX When we set __index in the metatable, Lua routes all method calls through here rather than checking for the
// existing key first. That's a bit surprising and I wonder whether there's a better way to handle this.
static int
LuaRemapIndex(lua_State * lua)
{
  LuaRemapRequest * rq;
  const char * index;

  rq = LuaRemapRequest::get(lua, 1);
  index = luaL_checkstring(lua, 2);

  LuaLogDebug("%s[%s]", __func__, index);

  // Get the userdata's metatable and look up the index in it.
  lua_getmetatable(lua, 1);
  lua_getfield(lua, -1, index);
  if (!lua_isnoneornil(lua, -1)) {
    // Pop the metatable, leaving the field value on top.
    lua_remove(lua, -2);
    return 1;
  }

  // Pop the field value and the metatable.
  lua_pop(lua, 2);

  lua_getfenv(lua, 1);

  // Get the requested field from the environment table.
  lua_getfield(lua, -1, index);

  // If we have a value for that field, pop the environment table, leaving the value on top.
  if (!lua_isnoneornil(lua, -1)) {
    lua_remove(lua, -2);
    return 1;
  }

  // Pop the nil field value.
  lua_pop(lua, 1);

  if (strcmp(index, "headers") == 0) {
    LuaRemapHeaders * hdrs;

    hdrs = LuaRemapHeaders::alloc(lua);
    hdrs->buffer = rq->rri->requestBufp;
    hdrs->headers = rq->rri->requestHdrp;

    // Set it for the 'headers' index and then push it on the stack.
    lua_setfield(lua, -2, index);
    lua_getfield(lua, -1, index);

    // Pop the environment table, leaving the field value on top.
    lua_remove(lua, -2);
    return 1;
  }

  return 0;
}

static const luaL_Reg RRI[] =
{
  { "redirect", LuaRemapRedirect },
  { "rewrite", LuaRemapRewrite },
  { "reject", LuaRemapReject },
  { "url", LuaRemapUrl },
  { "__index", LuaRemapIndex },
  { NULL, NULL}
};

static int
LuaRemapHeaderIndex(lua_State * lua)
{
  LuaRemapHeaders * hdrs;
  const char *      index;
  const char *      value;
  int               vlen;
  TSMLoc            field;

  hdrs = LuaRemapHeaders::get(lua, 1);;
  index = luaL_checkstring(lua, 2);

  LuaLogDebug("%s[%s]", __func__, index);

  field = TSMimeHdrFieldFind(hdrs->buffer, hdrs->headers, index, -1);
  if (field == TS_NULL_MLOC) {
    lua_pushnil(lua);
    return 1;
  }

  value = TSMimeHdrFieldValueStringGet(hdrs->buffer, hdrs->headers, field, -1, &vlen);
  lua_pushlstring(lua, value, vlen);
  return 1;
}

static int
LuaRemapHeaderNewIndex(lua_State * lua)
{
  LuaRemapHeaders * hdrs;
  const char *      index;
  const char *      value;
  size_t            vlen;
  TSMLoc            field;

  hdrs = LuaRemapHeaders::get(lua, 1);
  index = luaL_checkstring(lua, 2);

  LuaLogDebug("%s[%s] = (%s)", __func__, index, ltypeof(lua, 3));
  field = TSMimeHdrFieldFind(hdrs->buffer, hdrs->headers, index, -1);

  // Setting a key to nil means to delete it.
  if (lua_isnoneornil(lua, 3)) {
    if (field != TS_NULL_MLOC) {
      TSMimeHdrFieldDestroy(hdrs->buffer, hdrs->headers, field);
      TSHandleMLocRelease(hdrs->buffer, hdrs->headers, field);
    }

    return 1;
  }

  // If the MIME field doesn't exist yet, we'd better make it.
  if (field == TS_NULL_MLOC) {
    TSMimeHdrFieldCreateNamed(hdrs->buffer, hdrs->headers, index, -1, &field);
    TSMimeHdrFieldAppend(hdrs->buffer, hdrs->headers, field);
  }

  TSMimeHdrFieldValuesClear(hdrs->buffer, hdrs->headers, field);

  // Finally, we can set it's value.
  switch(lua_type(lua, 3)) {
    case LUA_TBOOLEAN:
      value = lua_toboolean(lua, 3) ? "1" : "0";
      vlen = 1;
      break;
    default:
      value = lua_tolstring(lua, 3, &vlen);
      break;
  }

  if (value) {
    TSMimeHdrFieldValueStringInsert(hdrs->buffer, hdrs->headers, field, -1, value, vlen);
  }

  TSHandleMLocRelease(hdrs->buffer, hdrs->headers, field);
  return 1;
}

static const luaL_Reg HEADERS[] =
{
  { "__index", LuaRemapHeaderIndex },
  { "__newindex", LuaRemapHeaderNewIndex },
  { NULL, NULL }
};

static int
LuaHttpTxnAbort(lua_State * lua)
{
  LuaHttpTransaction * txn;

  txn = LuaHttpTransaction::get(lua, 1);
  TSHttpTxnReenable(txn->txn, TS_EVENT_HTTP_ERROR);

  return 1;
}

static int
LuaHttpTxnContinue(lua_State * lua)
{
  LuaHttpTransaction * txn;

  txn = LuaHttpTransaction::get(lua, 1);
  TSHttpTxnReenable(txn->txn, TS_EVENT_HTTP_CONTINUE);

  return 1;
}

static int
LuaHttpTxnRegister(lua_State * lua)
{
  LuaHttpTransaction * txn;
  int tableref;

  txn = LuaHttpTransaction::get(lua, 1);
  luaL_checktype(lua, 2, LUA_TTABLE);

  // Keep a reference to the hooks table in ssn->hooks.
  tableref = luaL_ref(lua, LUA_REGISTRYINDEX);

  // On the other side of the denux, we need the hook, and the table.
  if (LuaRegisterHttpHooks(lua, txn->txn, LuaHttpTxnHookAdd, tableref)) {
    LuaSetArgReference(txn->txn, tableref);
    return 1;
  }

  return 0;
}

static int
LuaHttpTxnCacheLookupStatus(lua_State * lua)
{
  LuaHttpTransaction * txn;
  int status;

  txn = LuaHttpTransaction::get(lua, 1);
  if (TSHttpTxnCacheLookupStatusGet(txn->txn, &status) == TS_SUCCESS) {
    lua_pushinteger(lua, status);
  } else {
    lua_pushinteger(lua, -1);
  }

  return 1;
}

static const luaL_Reg HTTPTXN[] =
{
  { "abort", LuaHttpTxnAbort },
  { "continue", LuaHttpTxnContinue },
  { "register", LuaHttpTxnRegister },
  { "cachestatus", LuaHttpTxnCacheLookupStatus },
  { NULL, NULL }
};

static int
LuaHttpSsnAbort(lua_State * lua)
{
  LuaHttpSession * ssn;

  ssn = LuaHttpSession::get(lua, 1);
  TSHttpSsnReenable(ssn->ssn, TS_EVENT_HTTP_ERROR);

  return 1;
}

static int
LuaHttpSsnContinue(lua_State * lua)
{
  LuaHttpSession * ssn;

  ssn = LuaHttpSession::get(lua, 1);
  TSHttpSsnReenable(ssn->ssn, TS_EVENT_HTTP_CONTINUE);

  return 1;
}

static int
LuaHttpSsnRegister(lua_State * lua)
{
  LuaHttpSession * ssn;
  int tableref;

  ssn = LuaHttpSession::get(lua, 1);
  luaL_checktype(lua, 2, LUA_TTABLE);

  // Keep a reference to the hooks table in ssn->hooks.
  tableref = luaL_ref(lua, LUA_REGISTRYINDEX);

  // On the other side of the denux, we need the hook, and the table.
  if (LuaRegisterHttpHooks(lua, ssn->ssn, LuaHttpSsnHookAdd, tableref)) {
    LuaSetArgReference(ssn->ssn, tableref);
    return 1;
  }

  return 0;
}

static const luaL_Reg HTTPSSN[] =
{
  { "register", LuaHttpSsnRegister },
  { "abort", LuaHttpSsnAbort },
  { "continue", LuaHttpSsnContinue },
  { NULL, NULL }
};

static int
TSLuaDebug(lua_State * lua)
{
  const char * tag = luaL_checkstring(lua, 1);
  const char * message = luaL_checkstring(lua, 2);

  TSDebug(tag, "%s", message);
  return 0;
}

static const luaL_Reg LUAEXPORTS[] =
{
  { "debug", TSLuaDebug },
  { NULL, NULL}
};

LuaRemapRequest *
LuaPushRemapRequestInfo(lua_State * lua, TSHttpTxn txn, TSRemapRequestInfo * rri)
{
  return push_userdata_object<LuaRemapRequest>(lua, rri, txn);
}

LuaHttpTransaction *
LuaPushHttpTransaction(lua_State * lua, TSHttpTxn txn)
{
  return push_userdata_object<LuaHttpTransaction>(lua, txn);
}

LuaHttpSession *
LuaPushHttpSession(lua_State * lua, TSHttpSsn ssn)
{
  return push_userdata_object<LuaHttpSession>(lua, ssn);
}

int
LuaApiInit(lua_State * lua)
{
  LuaLogDebug("initializing TS API");

  lua_newtable(lua);

  // Register functions in the "ts" module.
  luaL_register(lua, NULL, LUAEXPORTS);

  // Push constants into the "ts" module.
  LuaSetConstantField(lua, "VERSION", TSTrafficServerVersionGet());
  LuaSetConstantField(lua, "MAJOR_VERSION", TSTrafficServerVersionGetMajor());
  LuaSetConstantField(lua, "MINOR_VERSION", TSTrafficServerVersionGetMinor());
  LuaSetConstantField(lua, "PATCH_VERSION", TSTrafficServerVersionGetPatch());

  LuaSetConstantField(lua, "CACHE_LOOKUP_MISS", TS_CACHE_LOOKUP_MISS);
  LuaSetConstantField(lua, "CACHE_LOOKUP_HIT_STALE", TS_CACHE_LOOKUP_HIT_STALE);
  LuaSetConstantField(lua, "CACHE_LOOKUP_HIT_FRESH", TS_CACHE_LOOKUP_HIT_FRESH);
  LuaSetConstantField(lua, "CACHE_LOOKUP_SKIPPED", TS_CACHE_LOOKUP_SKIPPED);

  // Register TSRemapRequestInfo metatable.
  LuaPushMetatable(lua, "ts.meta.rri", RRI);
  // Pop the metatable.
  lua_pop(lua, 1);

  // Register the remap headers metatable.
  LuaPushMetatable(lua, "ts.meta.rri.headers", HEADERS);
  // Pop the metatable.
  lua_pop(lua, 1);

  // Register TSHttpTxn metatable.
  LuaPushMetatable(lua, "ts.meta.http.txn", HTTPTXN);
  // Pop the metatable.
  lua_pop(lua, 1);

  // Register TSHttpSsn metatable.
  LuaPushMetatable(lua, "ts.meta.http.ssn", HTTPSSN);
  // Pop the metatable.
  lua_pop(lua, 1);

  TSReleaseAssert(lua_istable(lua, -1) == 1);
  return 1;
}
