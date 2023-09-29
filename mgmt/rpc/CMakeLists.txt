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
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
        ${CMAKE_SOURCE_DIR}/proxy/http

)

add_library(jsonrpc_protocol STATIC
        jsonrpc/error/RPCError.cc
        jsonrpc/JsonRPCManager.cc
        jsonrpc/Context.cc
)
add_library(ts::jsonrpc_protocol ALIAS jsonrpc_protocol)

# Some plugins link against ts::jsonrpc_protocol and therefore need it to be compiled as
# position independent.
set_target_properties(jsonrpc_protocol PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(jsonrpc_protocol
    PUBLIC
        ts::tscore
)

add_library(jsonrpc_server STATIC
        server/RPCServer.cc
        server/CommBase.cc
        server/IPCSocketServer.cc
        config/JsonRPCConfig.cc
)
add_library(ts::jsonrpc_server ALIAS jsonrpc_server)

target_link_libraries(jsonrpc_server
    PUBLIC
        ts::jsonrpc_protocol)

add_library(rpcpublichandlers STATIC
        handlers/common/RecordsUtils.cc
        handlers/config/Configuration.cc
        handlers/records/Records.cc
        handlers/storage/Storage.cc
        handlers/server/Server.cc
        handlers/plugins/Plugins.cc
)
add_library(ts::rpcpublichandlers ALIAS rpcpublichandlers)

target_link_libraries(rpcpublichandlers
    PUBLIC
        ts::tscore
    PRIVATE
        ts::inkcache
        ts::proxy
)

add_executable(test_jsonrpc
        jsonrpc/unit_tests/unit_test_main.cc
        jsonrpc/unit_tests/test_basic_protocol.cc
)
target_link_libraries(test_jsonrpc
        catch2::catch2
        ts::jsonrpc_protocol
        libswoc
)
add_test(NAME test_jsonrpc COMMAND test_jsonrpc)

add_executable(test_jsonrpcserver
        server/unit_tests/unit_test_main.cc
        server/unit_tests/test_rpcserver.cc
        ${CMAKE_SOURCE_DIR}/src/shared/rpc/IPCSocketClient.cc
)
target_link_libraries(test_jsonrpcserver
        catch2::catch2
        ts::jsonrpc_server
        ts::inkevent
        libswoc
)
add_test(NAME test_jsonrpcserver COMMAND test_jsonrpcserver)
