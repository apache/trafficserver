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

add_executable(traffic_server
        Crash.cc
        EventName.cc
        FetchSM.cc
        HostStatus.cc
        InkAPI.cc
        InkIOCoreAPI.cc
        SocksProxy.cc
        traffic_server.cc
        RpcAdminPubHandlers.cc
        ${CMAKE_SOURCE_DIR}/src/shared/overridable_txn_vars.cc
        )
target_include_directories(traffic_server PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/mgmt
        ${CMAKE_SOURCE_DIR}/mgmt/utils
)
target_link_libraries(traffic_server
    PRIVATE
        http
        http_remap
        http2
        logging
        hdrs
        configmanager
        diagsconfig
        utils_p
        inkutils
        inkdns
        inkhostdb
        inkcache
        fastlz
        aio
        tscore
        tscpputil
        proxy
        inknet
        records_p
        inkevent
        yaml-cpp
        libswoc
        jsonrpc_protocol
        jsonrpc_server
        rpcpublichandlers
)

if(TS_USE_QUIC)
    target_link_libraries(traffic_server
        PRIVATE
            ts::http3
            ts::quic
    )
endif()

if(TS_HAS_PROFILER)
    target_link_libraries(traffic_server PRIVATE gperftools::profiler)
endif()

if (TS_USE_LINUX_IO_URING)
    target_link_libraries(traffic_server PRIVATE inkuring uring)
endif (TS_USE_LINUX_IO_URING)
if (TS_USE_LINUX_NATIVE_AIO)
    target_link_libraries(traffic_server PRIVATE aio)
endif (TS_USE_LINUX_NATIVE_AIO)

set_target_properties(traffic_server PROPERTIES ENABLE_EXPORTS ON)

install(TARGETS traffic_server)
