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


add_library(inkhostdb STATIC
    HostDB.cc
    Inline.cc
    RefCountCache.cc
    HostFile.cc
    HostDBInfo.cc
)
add_library(ts::inkhostdb ALIAS inkhostdb)

target_include_directories(inkhostdb
    PUBLIC
        ${CMAKE_SOURCE_DIR}/iocore/hostdb
    PRIVATE
        ${CMAKE_SOURCE_DIR}/iocore/io_uring
        ${CMAKE_SOURCE_DIR}/iocore/dns
        ${CMAKE_SOURCE_DIR}/iocore/net
        ${CMAKE_SOURCE_DIR}/proxy/http
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
)

target_link_libraries(inkhostdb
    PUBLIC
        ts::inkcache
        ts::inkdns
        ts::inkevent
        ts::tscore
    PRIVATE
        ts::proxy
)
