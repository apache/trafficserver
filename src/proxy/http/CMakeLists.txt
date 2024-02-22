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
  http STATIC
  HttpSessionAccept.cc
  HttpBodyFactory.cc
  HttpCacheSM.cc
  Http1ClientSession.cc
  Http1ClientTransaction.cc
  Http1ServerTransaction.cc
  HttpConfig.cc
  HttpDebugNames.cc
  HttpProxyServerMain.cc
  HttpSM.cc
  Http1ServerSession.cc
  HttpSessionManager.cc
  HttpTransact.cc
  HttpTransactHeaders.cc
  HttpTunnel.cc
  HttpVCTable.cc
  ConnectingEntry.cc
  ForwardedConfig.cc
  PreWarmConfig.cc
  PreWarmManager.cc
)
add_library(ts::http ALIAS http)

if(BUILD_REGRESSION_TESTING)
  target_sources(http PRIVATE RegressionHttpTransact.cc)
endif()

target_link_libraries(
  http
  PUBLIC ts::inkevent ts::inkhostdb ts::proxy ts::tsutil ts::tscore
  PRIVATE ts::http2 ts::http_remap ts::inkcache ts::inkutils ts::logging
)

if(TS_USE_QUIC)
  target_link_libraries(http PRIVATE ts::http3)
endif()

if(BUILD_TESTING)
  add_subdirectory(unit_tests)
endif()

add_subdirectory(remap)

if(BUILD_TESTING)
  add_executable(
    test_proxy_http
    unit_tests/unit_test_main.cc unit_tests/test_ForwardedConfig.cc unit_tests/test_error_page_selection.cc
    unit_tests/test_PreWarm.cc ForwardedConfig.cc HttpBodyFactory.cc
  )
  target_link_libraries(test_proxy_http PRIVATE catch2::catch2 hdrs tscore inkevent proxy logging)
  add_test(NAME test_proxy_http COMMAND test_proxy_http)
endif(BUILD_TESTING)

clang_tidy_check(http)
