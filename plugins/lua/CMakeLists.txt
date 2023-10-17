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

add_atsplugin(
  tslua
  ts_lua.cc
  ts_lua_cached_response.cc
  ts_lua_client_request.cc
  ts_lua_client_response.cc
  ts_lua_client_response.cc
  ts_lua_context.cc
  ts_lua_hook.cc
  ts_lua_vconn.cc
  ts_lua_http.cc
  ts_lua_http_intercept.cc
  ts_lua_log.cc
  ts_lua_misc.cc
  ts_lua_server_request.cc
  ts_lua_server_response.cc
  ts_lua_transform.cc
  ts_lua_util.cc
  ts_lua_remap.cc
  ts_lua_http_cntl.cc
  ts_lua_http_milestone.cc
  ts_lua_http_txn_info.cc
  ts_lua_http_config.cc
  ts_lua_mgmt.cc
  ts_lua_package.cc
  ts_lua_string.cc
  ts_lua_crypto.cc
  ts_lua_stat.cc
  ts_lua_io.cc
  ts_lua_coroutine.cc
  ts_lua_fetch.cc
  ts_lua_constant.cc
)

target_include_directories(tslua PRIVATE "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(tslua PRIVATE LuaJIT::LuaJIT OpenSSL::Crypto)
