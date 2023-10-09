#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

lua_tslua_la_CPPFLAGS = $(AM_CPPFLAGS) $(LUAJIT_CPPFLAGS)
lua_tslua_la_LDFLAGS = $(AM_LDFLAGS) $(LUAJIT_LDFLAGS)

pkglib_LTLIBRARIES += lua/tslua.la

lua_tslua_la_SOURCES = \
  lua/ts_lua.c \
  lua/ts_lua_cached_response.c \
  lua/ts_lua_client_request.c \
  lua/ts_lua_client_response.c \
  lua/ts_lua_context.c \
  lua/ts_lua_hook.c \
  lua/ts_lua_vconn.c \
  lua/ts_lua_http.c \
  lua/ts_lua_http_intercept.c \
  lua/ts_lua_log.c \
  lua/ts_lua_misc.c \
  lua/ts_lua_server_request.c \
  lua/ts_lua_server_response.c \
  lua/ts_lua_transform.c \
  lua/ts_lua_util.c \
  lua/ts_lua_remap.c \
  lua/ts_lua_http_cntl.c \
  lua/ts_lua_http_milestone.c \
  lua/ts_lua_http_txn_info.c \
  lua/ts_lua_http_config.c \
  lua/ts_lua_mgmt.c \
  lua/ts_lua_package.c \
  lua/ts_lua_string.c \
  lua/ts_lua_crypto.c \
  lua/ts_lua_stat.c \
  lua/ts_lua_io.c \
  lua/ts_lua_coroutine.c \
  lua/ts_lua_fetch.c \
  lua/ts_lua_constant.c
