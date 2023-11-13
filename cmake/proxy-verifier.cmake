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

# This will download and extract proxy-verifier and setup variables to point to it.
#
# Required variables:
#   PROXY_VERIFIER_VERSION
#   PROXY_VERIFIER_HASH
#
# Defines variables:
#
#   PROXY_VERIFIER_PATH Full path to the extracted proxy verifier for the build architecture
#   PROXY_VERIFIER_CLIENT  Full path to client-verifier
#   PROXY_VERIFIER_SERVER  Full path to server-verifier

if(NOT PROXY_VERIFIER_VERSION)
  message(FATAL_ERROR "PROXY_VERIFIER_VERSION Required")
endif()

if(NOT PROXY_VERIFIER_HASH)
  message(FATAL_ERROR "PROXY_VERIFIER_HASH Required")
endif()

# Download proxy-verifier
set(PV_ARCHIVE ${CMAKE_BINARY_DIR}/proxy-verifier/proxy-verifier.tar.gz)
file(
  DOWNLOAD https://ci.trafficserver.apache.org/bintray/proxy-verifier-${PROXY_VERIFIER_VERSION}.tar.gz ${PV_ARCHIVE}
  EXPECTED_HASH ${PROXY_VERIFIER_HASH}
  SHOW_PROGRESS
)
file(ARCHIVE_EXTRACT INPUT ${PV_ARCHIVE})

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"
     OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "amd64"
     OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "i686"
  )
    set(PV_SUBDIR "linux-amd64")
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(PV_SUBDIR "linux-arm64")
  else()
    message(FATAL_ERROR "Unknown processor ${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"
     OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "amd64"
     OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "i686"
  )
    set(PV_SUBDIR "darwin-amd64")
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(PV_SUBDIR "darwin-arm64")
  else()
    message(FATAL_ERROR "Unknown processor ${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
else()
  message(FATAL_ERROR "Host ${CMAKE_HOST_SYSTEM_NAME} doesnt support running proxy verifier")
endif()

set(PROXY_VERIFIER_PATH ${CMAKE_BINARY_DIR}/proxy-verifier-${PROXY_VERIFIER_VERSION}/${PV_SUBDIR})
set(PROXY_VERIFIER_CLIENT ${PROXY_VERIFIER_PATH}/verifier-client)
set(PROXY_VERIFIER_SERVER ${PROXY_VERIFIER_PATH}/verifier-server)

message(STATUS "proxy-verifier setup in ${PROXY_VERIFIER_PATH}")
