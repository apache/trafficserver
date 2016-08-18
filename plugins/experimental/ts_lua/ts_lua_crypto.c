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

#include <openssl/md5.h>
#include <openssl/sha.h>
#include "ts_lua_string.h"
#include "ts_lua_util.h"

#define TS_LUA_MD5_DIGEST_LENGTH 16
#define TS_LUA_SHA_DIGEST_LENGTH 20

static int ts_lua_md5(lua_State *L);
static int ts_lua_md5_bin(lua_State *L);

static int ts_lua_sha1(lua_State *L);
static int ts_lua_sha1_bin(lua_State *L);

static int ts_lua_base64_encode(lua_State *L);
static int ts_lua_base64_decode(lua_State *L);

static int ts_lua_escape_uri(lua_State *L);
static int ts_lua_unescape_uri(lua_State *L);

void
ts_lua_inject_crypto_api(lua_State *L)
{
  /* ts.md5() */
  lua_pushcfunction(L, ts_lua_md5);
  lua_setfield(L, -2, "md5");

  /* ts.md5_bin(...) */
  lua_pushcfunction(L, ts_lua_md5_bin);
  lua_setfield(L, -2, "md5_bin");

  /* ts.sha1_bin(...) */
  lua_pushcfunction(L, ts_lua_sha1);
  lua_setfield(L, -2, "sha1");

  /* ts.sha1_bin(...) */
  lua_pushcfunction(L, ts_lua_sha1_bin);
  lua_setfield(L, -2, "sha1_bin");

  /* ts.base64_encode(...) */
  lua_pushcfunction(L, ts_lua_base64_encode);
  lua_setfield(L, -2, "base64_encode");

  /* ts.base64_decode(...) */
  lua_pushcfunction(L, ts_lua_base64_decode);
  lua_setfield(L, -2, "base64_decode");

  /* ts.escape_uri(...) */
  lua_pushcfunction(L, ts_lua_escape_uri);
  lua_setfield(L, -2, "escape_uri");

  /* ts.unescape_uri(...) */
  lua_pushcfunction(L, ts_lua_unescape_uri);
  lua_setfield(L, -2, "unescape_uri");
}

static int
ts_lua_md5(lua_State *L)
{
  u_char *src;
  size_t slen;

  MD5_CTX md5_ctx;
  u_char md5_buf[TS_LUA_MD5_DIGEST_LENGTH];
  u_char hex_buf[2 * sizeof(md5_buf)];

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;

  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  MD5_Init(&md5_ctx);
  MD5_Update(&md5_ctx, src, slen);
  MD5_Final(md5_buf, &md5_ctx);

  ts_lua_hex_dump(hex_buf, md5_buf, sizeof(md5_buf));

  lua_pushlstring(L, (char *)hex_buf, sizeof(hex_buf));

  return 1;
}

static int
ts_lua_md5_bin(lua_State *L)
{
  u_char *src;
  size_t slen;

  MD5_CTX md5_ctx;
  u_char md5_buf[TS_LUA_MD5_DIGEST_LENGTH];

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;

  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  MD5_Init(&md5_ctx);
  MD5_Update(&md5_ctx, src, slen);
  MD5_Final(md5_buf, &md5_ctx);

  lua_pushlstring(L, (char *)md5_buf, sizeof(md5_buf));

  return 1;
}

static int
ts_lua_sha1(lua_State *L)
{
  u_char *src;
  size_t slen;

  SHA_CTX sha;
  u_char sha_buf[TS_LUA_SHA_DIGEST_LENGTH];
  u_char hex_buf[2 * sizeof(sha_buf)];

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;

  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  SHA1_Init(&sha);
  SHA1_Update(&sha, src, slen);
  SHA1_Final(sha_buf, &sha);

  ts_lua_hex_dump(hex_buf, sha_buf, sizeof(sha_buf));
  lua_pushlstring(L, (char *)hex_buf, sizeof(hex_buf));

  return 1;
}

static int
ts_lua_sha1_bin(lua_State *L)
{
  u_char *src;
  size_t slen;

  SHA_CTX sha;
  u_char sha_buf[TS_LUA_SHA_DIGEST_LENGTH];

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;

  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  SHA1_Init(&sha);
  SHA1_Update(&sha, src, slen);
  SHA1_Final(sha_buf, &sha);

  lua_pushlstring(L, (char *)sha_buf, sizeof(sha_buf));

  return 1;
}

static int
ts_lua_base64_encode(lua_State *L)
{
  u_char *src;
  u_char *dst;
  size_t slen;
  size_t dlen;

  size_t length;

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;
  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  dlen = TS_LUA_MAX_STR_LENGTH;
  dst  = lua_newuserdata(L, dlen);

  if (TS_SUCCESS == TSBase64Encode((const char *)src, slen, (char *)dst, dlen, &length)) {
    lua_pushlstring(L, (char *)dst, length);
    return 1;
  } else {
    return luaL_error(L, "base64 encoding error");
  }
}

static int
ts_lua_base64_decode(lua_State *L)
{
  u_char *src;
  u_char *dst;
  size_t slen;
  size_t dlen;

  size_t length;

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument");
  }

  if (lua_isnil(L, 1)) {
    src  = (u_char *)"";
    slen = 0;
  } else {
    src = (u_char *)luaL_checklstring(L, 1, &slen);
  }

  dlen = TS_LUA_MAX_STR_LENGTH;
  dst  = lua_newuserdata(L, dlen);

  if (TS_SUCCESS == TSBase64Decode((const char *)src, slen, (unsigned char *)dst, dlen, &length)) {
    lua_pushlstring(L, (char *)dst, length);
    return 1;
  } else {
    return luaL_error(L, "base64 decoding error");
  }
}

static int
ts_lua_escape_uri(lua_State *L)
{
  size_t len, dlen;
  u_char *src, *dst;

  size_t length;

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument for ts.escape_uri(...)");
  }

  if (lua_isnil(L, 1)) {
    lua_pushliteral(L, "");
    return 1;
  }

  src = (u_char *)luaL_checklstring(L, 1, &len);
  if (len == 0)
    return 1;

  dlen = TS_LUA_MAX_STR_LENGTH;
  dst  = lua_newuserdata(L, dlen);

  if (TS_SUCCESS == TSStringPercentEncode((const char *)src, len, (char *)dst, dlen, &length, NULL)) {
    lua_pushlstring(L, (char *)dst, length);
    return 1;
  } else {
    return luaL_error(L, "percent encoding error");
  }
}

static int
ts_lua_unescape_uri(lua_State *L)
{
  size_t len, dlen;
  u_char *src, *dst;

  size_t length;

  if (lua_gettop(L) != 1) {
    return luaL_error(L, "expecting one argument for ts.unescape_uri(...)");
  }

  if (lua_isnil(L, 1)) {
    lua_pushliteral(L, "");
    return 1;
  }

  src = (u_char *)luaL_checklstring(L, 1, &len);
  if (len == 0)
    return 1;

  /* the unescaped string can only be smaller */
  dlen = len;
  dst  = lua_newuserdata(L, dlen);

  if (TS_SUCCESS == TSStringPercentDecode((const char *)src, len, (char *)dst, dlen, &length)) {
    lua_pushlstring(L, (char *)dst, length);
    return 1;
  } else {
    return luaL_error(L, "percent decoding error");
  }
}
