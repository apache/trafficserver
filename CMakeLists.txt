#### EDITOR SUPPORT ####
## This file exists ONLY to support code editors (e.g. Visual Code, CLion).
##
## !!!-> IT WILL NOT BUILD TRAFFIC SERVER <-!!!
##
## This file can be tweaked to keep editors happy. In general all projects should be defined here so
## there is only one instance of this file, not one per directory. This has been done only to the
## extent that someone using such an editor needed it, so much work is left to be done.
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

cmake_minimum_required(VERSION 3.7)
project(ats)

set(CMAKE_CXX_STANDARD 17)

include_directories(
        include
        tests/include
        lib
        proxy
        proxy/hdrs
        proxy/http
        proxy/http/remap
        proxy/shared
        iocore/eventsystem
        iocore/net
        iocore/dns
        iocore/hostdb
        iocore/aio
        iocore/cache
        iocore/utils
        mgmt
        mgmt/api
        mgmt/utils
        mgmt/api/include
        proxy/api)

macro(CC_EXEC exec path)
    file(GLOB cc_exec_files ${path}/*.cc ${path}/*.h)
    add_executable(${exec} ${cc_exec_files})
endmacro(CC_EXEC)

macro(CPP_LIB name src_path inc_path)
    file(GLOB cc_lib_files ${inc_path}/*.h ${src_path}/*.cc ${src_path}/*.h ${src_path}/unit_tests/*.cc)
    add_library(${name} SHARED ${cc_lib_files})
endmacro(CPP_LIB)

macro(CPP_ADD_SOURCES target path)
    file(GLOB cpp_add_src_files ${path}/*.h ${path}/*.cc)
    target_sources(${target} PUBLIC ${cpp_add_src_files})
endmacro(CPP_ADD_SOURCES)

CC_EXEC(traffic_cache_tool src/traffic_cache_tool)
CC_EXEC(traffic_crashlog src/traffic_crashlog)
CC_EXEC(traffic_ctl src/traffic_ctl)
CC_EXEC(traffic_layout src/traffic_layout)
CC_EXEC(traffic_logcat src/traffic_logcat)
CC_EXEC(traffic_logstats src/traffic_logstats)
CC_EXEC(traffic_manager src/traffic_manager)
CC_EXEC(traffic_server src/traffic_server)
CC_EXEC(traffic_top src/traffic_top)
CC_EXEC(traffic_via src/traffic_via)
CC_EXEC(traffic_wccp src/traffic_wccp)

CPP_LIB(tscore src/tscore include/tscore)
CPP_LIB(tscpputil src/tscpp/util include/tscpp/util)
CPP_LIB(tscppapi src/tscpp/api include/tscpp/api)

CC_EXEC(test_tscore src/tscore/unit_tests)
CC_EXEC(test_tsutil src/tscpp/util/unit_tests)
CC_EXEC(test_librecords lib/records/unit_tests)

CPP_LIB(proxy proxy proxy)
CPP_ADD_SOURCES(proxy proxy/http)
CPP_ADD_SOURCES(proxy proxy/http/unit_tests)
CPP_ADD_SOURCES(proxy proxy/http2)
CPP_ADD_SOURCES(proxy proxy/http/remap)
CPP_ADD_SOURCES(proxy proxy/hdrs)
CPP_ADD_SOURCES(proxy proxy/hdrs/unit_tests)
CPP_ADD_SOURCES(proxy proxy/logging)

CPP_LIB(iocore iocore iocore)
CPP_ADD_SOURCES(iocore iocore/eventsystem)
CPP_ADD_SOURCES(iocore iocore/net)
CPP_ADD_SOURCES(iocore iocore/cache)
CPP_ADD_SOURCES(iocore iocore/aio)
CPP_ADD_SOURCES(iocore iocore/dns)
CPP_ADD_SOURCES(iocore iocore/hostdb)
CPP_ADD_SOURCES(iocore iocore/utils)

CPP_LIB(mgmt mgmt mgmt)
CPP_ADD_SOURCES(mgmt mgmt/api)
CPP_ADD_SOURCES(mgmt mgmt/utils)

CPP_LIB(records lib/records lib/records)
CPP_LIB(logging proxy/logging proxy/logging)

CPP_LIB(wccp src/wccp include/wccp)

file(GLOB plugin_files
        plugins/*/*.h
        plugins/*/*.c
        plugins/*/*.cc
        plugins/experimental/*/*.h
        plugins/experimental/*/*.c
        plugins/experimental/*/*.cc
        example/*/*.h
        example/*/*.c
        example/*/*.cc
        )
add_library(plugins SHARED ${plugin_files})

add_custom_target(clang-format WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY} COMMAND make -j clang-format)
