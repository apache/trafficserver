#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

find_package(Lua REQUIRED)
find_package(LuaJIT REQUIRED)

# GCC warns that -Wno-invalid-offset, set in top-level CMakeLists.txt,
# is invalid for C language
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

add_atsplugin(tslua
    ts_lua.c
    ts_lua_cached_response.c
    ts_lua_client_request.c
    ts_lua_client_response.c
    ts_lua_client_response.c
    ts_lua_context.c
    ts_lua_http.c
    ts_lua_http_intercept.c
    ts_lua_log.c
    ts_lua_misc.c
    ts_lua_server_request.c
    ts_lua_server_response.c
    ts_lua_transform.c
    ts_lua_util.c
    ts_lua_remap.c
    ts_lua_http_cntl.c
    ts_lua_http_milestone.c
    ts_lua_http_txn_info.c
    ts_lua_http_config.c
    ts_lua_mgmt.c
    ts_lua_package.c
    ts_lua_string.c
    ts_lua_crypto.c
    ts_lua_stat.c
    ts_lua_io.c
    ts_lua_coroutine.c
    ts_lua_fetch.c
    ts_lua_constant.c
)

target_include_directories(tslua PRIVATE "${LUA_INCLUDE_DIRS}")

target_link_libraries(tslua PRIVATE ${LUA_LIBRARY})

target_compile_options(healthchecks PRIVATE -Wall -Wextra)
