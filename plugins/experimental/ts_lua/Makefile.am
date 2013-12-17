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

include $(top_srcdir)/build/plugins.mk

if BUILD_LUA_SUPPORT

# We have to use the per-target CPPFLAGS here to make sure that the Lua include
# path comes first. If we have LuaJIT (/opt/local/include/luajit-2.0) and Lua
# (/opt/local/include) headers, then we need to make sure that we don't end up
# building with the Lua headers but linking LuaJIT.
tslua_la_CPPFLAGS = \
  $(LUA_CFLAGS) \
  $(AM_CPPFLAGS)

pkglib_LTLIBRARIES = tslua.la

tslua_la_LIBADD = $(LUA_LIBS)
tslua_la_SOURCES = ts_lua.c ts_lua_cached_response.c ts_lua_client_request.c ts_lua_client_response.c ts_lua_context.c ts_lua_hook.c ts_lua_http.c ts_lua_http_intercept.c ts_lua_log.c ts_lua_misc.c ts_lua_server_request.c ts_lua_server_response.c ts_lua_transform.c ts_lua_util.c ts_lua_remap.c
tslua_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

endif
