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


add_library(inkcache STATIC
    Cache.cc
    CacheDir.cc
    CacheDisk.cc
    CacheHosting.cc
    CacheHttp.cc
    CachePages.cc
    CachePagesInternal.cc
    CacheRead.cc
    CacheVC.cc
    CacheVol.cc
    CacheWrite.cc
    HttpTransactCache.cc
    RamCacheCLFUS.cc
    RamCacheLRU.cc
    Store.cc
)
add_library(ts::inkcache ALIAS inkcache)

if(BUILD_REGRESSION_TESTING)
    target_sources(inkcache PRIVATE CacheTest.cc)
    target_link_libraries(inkcache PRIVATE ts::proxy)
endif()

target_include_directories(inkcache
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include/iocore/cache
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/iocore/io_uring
        ${CMAKE_SOURCE_DIR}/include/iocore/dns
        ${CMAKE_SOURCE_DIR}/include/iocore/net
        ${CMAKE_SOURCE_DIR}/include/iocore/hostdb
        ${CMAKE_SOURCE_DIR}/proxy/http
        ${CMAKE_SOURCE_DIR}/proxy/http/remap
        ${CMAKE_SOURCE_DIR}/proxy/hdrs
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/test
)

target_link_libraries(inkcache
    PUBLIC
        ts::aio
        ts::hdrs
        ts::http
        ts::inkevent
        ts::tscore
    PRIVATE
        ts::proxy
        fastlz
        ZLIB::ZLIB
)

if(HAVE_LZMA_H)
    target_link_libraries(inkcache PRIVATE LibLZMA::LibLZMA)
endif()
