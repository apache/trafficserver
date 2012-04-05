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

#include <ts/ts.h>
#include <ts/remap.h>
#include <string.h>
#include "lapi.h"
#include "lutil.h"

// Return the type name string for the given index.
#define LTYPEOF(L, index) lua_typename(L, lua_type(L, index))

struct LuaRemapHeaders
{
  TSMBuffer buffer;
  TSMLoc    headers;

  static LuaRemapHeaders * get(lua_State * lua, int index) {
    return (LuaRemapHeaders *)luaL_checkudata(lua, index, "ts.meta.rri.headers");
  }

  static LuaRemapHeaders * alloc(lua_State * lua) {
    LuaRemapHeaders * hdrs;

    hdrs = (LuaRemapHeaders *)lua_newuserdata(lua, sizeof(LuaRemapHeaders));
    luaL_getmetatable(lua, "ts.meta.rri.headers");
    lua_setmetatable(lua, -2);

    return hdrs;
  }
};

LuaRemapRequest *
LuaRemapRequest::get(lua_State * lua, int index)
{
  return (LuaRemapRequest *)luaL_checkudata(lua, index, "ts.meta.rri");
}

LuaRemapRequest *
LuaRemapRequest::alloc(lua_State * lua)
{
  LuaRemapRequest * rq;

  rq = (LuaRemapRequest *)lua_newuserdata(lua, sizeof(LuaRemapRequest));
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

  TSDebug("lua", "redirecting request %p", rq->rri);

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

  TSDebug("lua", "rewriting request %p", rq->rri);

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

  TSDebug("lua", "rejecting request %p with status %d", rq->rri, status);

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

  TSDebug("lua", "%s[%s]", __func__, index);

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

  TSDebug("lua", "%s[%s]", __func__, index);

  field = TSMimeHdrFieldFind(hdrs->buffer, hdrs->headers, index, -1);
  if (field == TS_NULL_MLOC) {
    lua_pushnil(lua);
    return 1;
  }

  value = TSMimeHdrFieldValueStringGet(hdrs->buffer, hdrs->headers, field, 0, &vlen);
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

  TSDebug("lua", "%s[%s] = (%s)", __func__, index, LTYPEOF(lua, 3));
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

LuaRemapRequest *
LuaPushRemapRequestInfo(lua_State * lua, TSHttpTxn txn, TSRemapRequestInfo * rri)
{
  LuaRemapRequest * rq;

  rq = LuaRemapRequest::alloc(lua);
  rq->rri = rri;
  rq->txn = txn;
  rq->status = TSREMAP_NO_REMAP;

  TSReleaseAssert(lua_isuserdata(lua, -1) == 1);
  return rq;
}

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

int
LuaApiInit(lua_State * lua)
{
  TSDebug("lua", "initializing Lua API");

  lua_newtable(lua);

  // Register functions in the "ts" module.
  luaL_register(lua, NULL, LUAEXPORTS);

  // Push constants into the "ts" module.
  lua_pushstring(lua, TSTrafficServerVersionGet());
  lua_setfield(lua, -2, "VERSION");

  lua_pushinteger(lua, TSTrafficServerVersionGetMajor());
  lua_setfield(lua, -2, "MAJOR_VERSION");

  lua_pushinteger(lua, TSTrafficServerVersionGetMinor());
  lua_setfield(lua, -2, "MINOR_VERSION");

  lua_pushinteger(lua, TSTrafficServerVersionGetPatch());
  lua_setfield(lua, -2, "PATCH_VERSION");

  lua_pushinteger(lua, TSREMAP_DID_REMAP_STOP);
  lua_setfield(lua, -2, "REMAP_COMPLETE");

  lua_pushinteger(lua, TSREMAP_DID_REMAP);
  lua_setfield(lua, -2, "REMAP_CONTINUE");

  // Register TSRemapRequestInfo metatable.
  LuaPushMetatable(lua, "ts.meta.rri", RRI);
  // Pop the metatable.
  lua_pop(lua, 1);

  // Register the remap headers metatable.
  LuaPushMetatable(lua, "ts.meta.rri.headers", HEADERS);
  // Pop the metatable.
  lua_pop(lua, 1);

  TSReleaseAssert(lua_istable(lua, -1) == 1);
  return 1;
}
