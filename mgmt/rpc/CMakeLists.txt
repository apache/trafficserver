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

include_directories(
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/rpc
        ${IOCORE_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_SOURCE_DIR}/proxy
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
        ${CMAKE_SOURCE_DIR}/proxy/http

)

add_library(jsonrpc_protocol STATIC
        jsonrpc/error/RPCError.cc
        jsonrpc/JsonRPCManager.cc
        jsonrpc/Context.cc
)
add_library(ts::jsonrpc_protocol ALIAS jsonrpc_protocol)

add_library(jsonrpc_server STATIC
        server/RPCServer.cc
        server/CommBase.cc
        server/IPCSocketServer.cc
        config/JsonRPCConfig.cc
)
add_library(ts::jsonrpc_server ALIAS jsonrpc_server)

add_library(rpcpublichandlers STATIC
        handlers/common/RecordsUtils.cc
        handlers/config/Configuration.cc
        handlers/records/Records.cc
        handlers/storage/Storage.cc
        handlers/server/Server.cc
        handlers/plugins/Plugins.cc
)
add_library(ts::rpcpublichandlers ALIAS rpcpublichandlers)
