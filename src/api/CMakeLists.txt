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

# plugin api *only*
add_library(tsapi SHARED InkAPI.cc InkAPIInternal.cc InkIOCoreAPI.cc)
add_library(ts::tsapi ALIAS tsapi)

set(TSAPI_PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/include/ts/ts.h ${PROJECT_SOURCE_DIR}/include/ts/remap.h
                         ${PROJECT_SOURCE_DIR}/include/ts/TsException.h ${PROJECT_BINARY_DIR}/include/ts/apidefs.h
)

target_link_libraries(tsapi PRIVATE libswoc::libswoc yaml-cpp::yaml-cpp PCRE::PCRE OpenSSL::SSL)
set_target_properties(tsapi PROPERTIES PUBLIC_HEADER "${TSAPI_PUBLIC_HEADERS}")

# Items common between api and other ts libraries
add_library(
  tsapibackend STATIC # need to fix circular dep to remove this
  ConfigUpdateCbTable.cc # configmanager
  InkContInternal.cc # proxy, http
  InkVConnInternal.cc # proxy, http
  APIHook.cc # iocore
  APIHooks.cc # proxy
  LifecycleAPIHooks.cc # proxy, http
  HttpHookState.cc # proxy, http
)
add_library(ts::tsapibackend ALIAS tsapibackend)
target_link_libraries(
  tsapibackend
  PUBLIC ts::inknet ts::http
  PRIVATE ts::proxy ts::hdrs ts::tscore
)

install(
  TARGETS tsapi
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ts
)

install(
  TARGETS tsapibackend
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(APPLE)
  target_link_options(tsapi PRIVATE -undefined dynamic_lookup)
endif()
