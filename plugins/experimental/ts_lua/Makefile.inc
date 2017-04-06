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

experimental_ts_lua_tslua_la_CPPFLAGS = $(AM_CPPFLAGS) $(LUAJIT_CPPFLAGS)

pkglib_LTLIBRARIES += experimental/ts_lua/tslua.la

experimental_ts_lua_tslua_la_SOURCES = \
  experimental/ts_lua/ts_lua.c \
  experimental/ts_lua/ts_lua_cached_response.c \
  experimental/ts_lua/ts_lua_client_request.c \
  experimental/ts_lua/ts_lua_client_response.c \
  experimental/ts_lua/ts_lua_context.c \
  experimental/ts_lua/ts_lua_hook.c \
  experimental/ts_lua/ts_lua_http.c \
  experimental/ts_lua/ts_lua_http_intercept.c \
  experimental/ts_lua/ts_lua_log.c \
  experimental/ts_lua/ts_lua_misc.c \
  experimental/ts_lua/ts_lua_server_request.c \
  experimental/ts_lua/ts_lua_server_response.c \
  experimental/ts_lua/ts_lua_transform.c \
  experimental/ts_lua/ts_lua_util.c \
  experimental/ts_lua/ts_lua_remap.c \
  experimental/ts_lua/ts_lua_http_cntl.c \
  experimental/ts_lua/ts_lua_http_milestone.c \
  experimental/ts_lua/ts_lua_http_config.c \
  experimental/ts_lua/ts_lua_mgmt.c \
  experimental/ts_lua/ts_lua_package.c \
  experimental/ts_lua/ts_lua_string.c \
  experimental/ts_lua/ts_lua_crypto.c \
  experimental/ts_lua/ts_lua_stat.c \
  experimental/ts_lua/ts_lua_io.c \
  experimental/ts_lua/ts_lua_coroutine.c \
  experimental/ts_lua/ts_lua_fetch.c \
  experimental/ts_lua/ts_lua_constant.c \
  experimental/ts_lua/ts_lua_slab.c \
  experimental/ts_lua/ts_lua_shdict.c \
  experimental/ts_lua/ts_lua_base_rbtree.c \
  experimental/ts_lua/ts_lua_base_crc32.c 
