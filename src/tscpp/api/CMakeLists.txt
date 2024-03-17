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

add_library(
  tscppapi SHARED
  AsyncHttpFetch.cc
  AsyncTimer.cc
  CaseInsensitiveStringComparator.cc
  ClientRequest.cc
  Continuation.cc
  GlobalPlugin.cc
  GzipDeflateTransformation.cc
  GzipInflateTransformation.cc
  Headers.cc
  HttpMethod.cc
  HttpVersion.cc
  InterceptPlugin.cc
  Logger.cc
  Plugin.cc
  RemapPlugin.cc
  Request.cc
  Response.cc
  Stat.cc
  Transaction.cc
  TransactionPlugin.cc
  TransformationPlugin.cc
  Url.cc
  utils.cc
  utils_internal.cc
)
add_library(ts::tscppapi ALIAS tscppapi)
target_link_libraries(tscppapi PUBLIC libswoc::libswoc yaml-cpp::yaml-cpp)

if(APPLE)
  target_link_options(tscppapi PRIVATE -undefined dynamic_lookup)
endif()

set(TSCPP_API_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Async.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/AsyncHttpFetch.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/AsyncTimer.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/CaseInsensitiveStringComparator.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Cleanup.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/ClientRequest.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Continuation.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/GlobalPlugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/GzipDeflateTransformation.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/GzipInflateTransformation.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Headers.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/HttpMethod.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/HttpStatus.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/HttpVersion.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/InterceptPlugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Logger.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Plugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/PluginInit.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/RemapPlugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Request.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Response.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Stat.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Transaction.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/TransactionPlugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/TransformationPlugin.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/Url.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/noncopyable.h
    ${PROJECT_SOURCE_DIR}/include/tscpp/api/utils.h
)
set_target_properties(tscppapi PROPERTIES PUBLIC_HEADER "${TSCPP_API_PUBLIC_HEADERS}")

install(TARGETS tscppapi PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tscpp/api)

clang_tidy_check(tscppapi)
