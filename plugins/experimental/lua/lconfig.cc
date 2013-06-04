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

#include "lapi.h"
#include "lutil.h"

// ts.config.override(txn, key, value)
//
// Override a configuration entry for this transaction.
static int
TSLuaConfigOverride(lua_State * lua)
{
  LuaRemapRequest *       rq;
  TSOverridableConfigKey  key;
  union {
    lua_Number  number;
    lua_Integer integer;
    struct { const char * str; size_t len; } lstring;
  } value;


  // XXX For now, this only works on remap request objects. When we expose a TSHttpTxn object in Lua, we should
  // dynamically support passing one of those in as well.
  rq = LuaRemapRequest::get(lua, 1);

  key = (TSOverridableConfigKey)luaL_checkint(lua, 2);

  switch(lua_type(lua, 3)) {
    case LUA_TBOOLEAN:
      TSHttpTxnConfigIntSet(rq->txn, key, lua_toboolean(lua, 3) ? 1 : 0);
      break;
    case LUA_TNUMBER:
      // There's no API that will tell us the correct type to use for numberic override options. Let's try int first,
      // since that's the common case. If that fails we can try float.
      value.integer = luaL_checkinteger(lua, 3);
      if (TSHttpTxnConfigIntSet(rq->txn, key, value.integer) == TS_ERROR) {
        value.number = luaL_checknumber(lua, 3);
        TSHttpTxnConfigFloatSet(rq->txn, key, value.number);
      }

      break;
    case LUA_TSTRING:
      value.lstring.str = lua_tolstring(lua, 3, &value.lstring.len);
      TSHttpTxnConfigStringSet(rq->txn, key, value.lstring.str, value.lstring.len);
      break;
  }

  return 0;
}

static const luaL_Reg LUAEXPORTS[] =
{
  { "override", TSLuaConfigOverride },
  { NULL, NULL}
};

int
LuaConfigApiInit(lua_State * lua)
{
  LuaLogDebug("initializing TS Config API");

  lua_newtable(lua);

  // Register functions in the "ts.config" module.
  luaL_register(lua, NULL, LUAEXPORTS);

#define DEFINE_CONFIG_KEY(NAME) LuaSetConstantField(lua, #NAME, TS_CONFIG_ ## NAME)
  DEFINE_CONFIG_KEY(URL_REMAP_PRISTINE_HOST_HDR);
  DEFINE_CONFIG_KEY(HTTP_CHUNKING_ENABLED);
  DEFINE_CONFIG_KEY(HTTP_NEGATIVE_CACHING_ENABLED);
  DEFINE_CONFIG_KEY(HTTP_NEGATIVE_CACHING_LIFETIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_WHEN_TO_REVALIDATE);
  DEFINE_CONFIG_KEY(HTTP_KEEP_ALIVE_ENABLED_IN);
  DEFINE_CONFIG_KEY(HTTP_KEEP_ALIVE_ENABLED_OUT);
  DEFINE_CONFIG_KEY(HTTP_KEEP_ALIVE_POST_OUT);
  DEFINE_CONFIG_KEY(HTTP_SHARE_SERVER_SESSIONS);
  DEFINE_CONFIG_KEY(NET_SOCK_RECV_BUFFER_SIZE_OUT);
  DEFINE_CONFIG_KEY(NET_SOCK_SEND_BUFFER_SIZE_OUT);
  DEFINE_CONFIG_KEY(NET_SOCK_OPTION_FLAG_OUT);
  DEFINE_CONFIG_KEY(HTTP_FORWARD_PROXY_AUTH_TO_PARENT);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_REMOVE_FROM);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_REMOVE_REFERER);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_REMOVE_USER_AGENT);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_REMOVE_COOKIE);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_REMOVE_CLIENT_IP);
  DEFINE_CONFIG_KEY(HTTP_ANONYMIZE_INSERT_CLIENT_IP);
  DEFINE_CONFIG_KEY(HTTP_RESPONSE_SERVER_ENABLED);
  DEFINE_CONFIG_KEY(HTTP_INSERT_SQUID_X_FORWARDED_FOR);
  DEFINE_CONFIG_KEY(HTTP_SERVER_TCP_INIT_CWND);
  DEFINE_CONFIG_KEY(HTTP_SEND_HTTP11_REQUESTS);
  DEFINE_CONFIG_KEY(HTTP_CACHE_HTTP);
  DEFINE_CONFIG_KEY(HTTP_CACHE_IGNORE_CLIENT_NO_CACHE);
  DEFINE_CONFIG_KEY(HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE);
  DEFINE_CONFIG_KEY(HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE);
  DEFINE_CONFIG_KEY(HTTP_CACHE_IGNORE_SERVER_NO_CACHE);
  DEFINE_CONFIG_KEY(HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES);
  DEFINE_CONFIG_KEY(HTTP_CACHE_IGNORE_AUTHENTICATION);
  DEFINE_CONFIG_KEY(HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC);
  DEFINE_CONFIG_KEY(HTTP_CACHE_REQUIRED_HEADERS);
  DEFINE_CONFIG_KEY(HTTP_INSERT_REQUEST_VIA_STR);
  DEFINE_CONFIG_KEY(HTTP_INSERT_RESPONSE_VIA_STR);
  DEFINE_CONFIG_KEY(HTTP_CACHE_HEURISTIC_MIN_LIFETIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_HEURISTIC_MAX_LIFETIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_GUARANTEED_MIN_LIFETIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_GUARANTEED_MAX_LIFETIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_MAX_STALE_AGE);
  DEFINE_CONFIG_KEY(HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN);
  DEFINE_CONFIG_KEY(HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT);
  DEFINE_CONFIG_KEY(HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN);
  DEFINE_CONFIG_KEY(HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT);
  DEFINE_CONFIG_KEY(HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT);
  DEFINE_CONFIG_KEY(HTTP_ORIGIN_MAX_CONNECTIONS);
  DEFINE_CONFIG_KEY(HTTP_CONNECT_ATTEMPTS_MAX_RETRIES);
  DEFINE_CONFIG_KEY(HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER);
  DEFINE_CONFIG_KEY(HTTP_CONNECT_ATTEMPTS_RR_RETRIES);
  DEFINE_CONFIG_KEY(HTTP_CONNECT_ATTEMPTS_TIMEOUT);
  DEFINE_CONFIG_KEY(HTTP_POST_CONNECT_ATTEMPTS_TIMEOUT);
  DEFINE_CONFIG_KEY(HTTP_DOWN_SERVER_CACHE_TIME);
  DEFINE_CONFIG_KEY(HTTP_DOWN_SERVER_ABORT_THRESHOLD);
  DEFINE_CONFIG_KEY(HTTP_CACHE_FUZZ_TIME);
  DEFINE_CONFIG_KEY(HTTP_CACHE_FUZZ_MIN_TIME);
  DEFINE_CONFIG_KEY(HTTP_DOC_IN_CACHE_SKIP_DNS);
  DEFINE_CONFIG_KEY(HTTP_RESPONSE_SERVER_STR);
  DEFINE_CONFIG_KEY(HTTP_CACHE_HEURISTIC_LM_FACTOR);
  DEFINE_CONFIG_KEY(HTTP_CACHE_FUZZ_PROBABILITY);
  DEFINE_CONFIG_KEY(NET_SOCK_PACKET_MARK_OUT);
  DEFINE_CONFIG_KEY(NET_SOCK_PACKET_TOS_OUT);
  DEFINE_CONFIG_KEY(HTTP_INSERT_AGE_IN_RESPONSE);
#undef DEFINE_CONFIG_KEY

  return 1;
}
