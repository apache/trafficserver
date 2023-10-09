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

#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts_lua_util.h"

static int ts_lua_vconn_get_remote_addr(lua_State *L);
static int ts_lua_vconn_get_fd(lua_State *L);

void
ts_lua_inject_vconn_api(lua_State *L)
{
  lua_newtable(L);

  lua_pushcfunction(L, ts_lua_vconn_get_remote_addr);
  lua_setfield(L, -2, "get_remote_addr");

  lua_pushcfunction(L, ts_lua_vconn_get_fd);
  lua_setfield(L, -2, "get_fd");

  lua_setfield(L, -2, "vconn");
}

static int
ts_lua_vconn_get_remote_addr(lua_State *L)
{
  ts_lua_vconn_ctx *vconn_ctx;
  int port;
  int family;
  char sip[128];

  GET_VCONN_CONTEXT(vconn_ctx, L);

  struct sockaddr const *addr = TSNetVConnRemoteAddrGet(vconn_ctx->vconn);

  if (addr == NULL) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
  } else {
    if (addr->sa_family == AF_INET) {
      port = ntohs(((struct sockaddr_in *)addr)->sin_port);
      inet_ntop(AF_INET, (const void *)&((struct sockaddr_in *)addr)->sin_addr, sip, sizeof(sip));
      family = AF_INET;
    } else {
      port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
      inet_ntop(AF_INET6, (const void *)&((struct sockaddr_in6 *)addr)->sin6_addr, sip, sizeof(sip));
      family = AF_INET6;
    }

    lua_pushstring(L, sip);
    lua_pushnumber(L, port);
    lua_pushnumber(L, family);
  }

  return 3;
}

static int
ts_lua_vconn_get_fd(lua_State *L)
{
  int fd = 0;
  ts_lua_vconn_ctx *vconn_ctx;

  GET_VCONN_CONTEXT(vconn_ctx, L);

  fd = TSVConnFdGet(vconn_ctx->vconn);

  lua_pushnumber(L, fd);

  return 1;
}
