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
    lua/ts_lua.cc \
    lua/ts_lua_cached_response.cc \
    lua/ts_lua_client_request.cc \
    lua/ts_lua_client_response.cc \
    lua/ts_lua_context.cc \
    lua/ts_lua_hook.cc \
    lua/ts_lua_vconn.cc \
    lua/ts_lua_http.cc \
    lua/ts_lua_http_intercept.cc \
    lua/ts_lua_log.cc \
    lua/ts_lua_misc.cc \
    lua/ts_lua_server_request.cc \
    lua/ts_lua_server_response.cc \
    lua/ts_lua_transform.cc \
    lua/ts_lua_util.cc \
    lua/ts_lua_remap.cc \
    lua/ts_lua_http_cntl.cc \
    lua/ts_lua_http_milestone.cc \
    lua/ts_lua_http_txn_info.cc \
    lua/ts_lua_http_config.cc \
    lua/ts_lua_mgmt.cc \
    lua/ts_lua_package.cc \
    lua/ts_lua_string.cc \
    lua/ts_lua_crypto.cc \
    lua/ts_lua_stat.cc \
    lua/ts_lua_io.cc \
    lua/ts_lua_coroutine.cc \
    lua/ts_lua_fetch.cc \
    lua/ts_lua_constant.cc
