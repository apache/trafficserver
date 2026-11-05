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

find_package(Doxygen 1.9.7 REQUIRED COMPONENTS doxygen dot)
find_package(Java REQUIRED COMPONENTS Runtime)
find_file(
  PLANTUML_JAR
  NAMES plantuml.jar
  PATHS /usr/share/java /usr/local/share/java
  PATH_SUFFIXES plantuml REQUIRED
)

set(DOXYGEN_HAS_OPENSSL_QUIC 0)
set(DOXYGEN_HAS_QUICHE 0)
if(TS_HAS_OPENSSL_QUIC)
  set(DOXYGEN_HAS_OPENSSL_QUIC 1)
endif()
if(TS_HAS_QUICHE)
  set(DOXYGEN_HAS_QUICHE 1)
endif()
# These legacy sources are no longer part of any CMake target.
set(DOXYGEN_EXCLUDE
    "\"${CMAKE_SOURCE_DIR}/src/traffic_quic\" \"${CMAKE_SOURCE_DIR}/src/iocore/net/quic/QUICDebugNames.cc\""
)
if(NOT TS_HAS_OPENSSL_QUIC)
  foreach(source OpenSSLQUICNetProcessor.cc OpenSSLQUICNetVConnection.cc OpenSSLQUICPacketHandler.cc)
    string(APPEND DOXYGEN_EXCLUDE " \"${CMAKE_SOURCE_DIR}/src/iocore/net/${source}\"")
  endforeach()
endif()
if(NOT TS_HAS_QUICHE OR TS_HAS_OPENSSL_QUIC)
  foreach(source QUICNet.cc QUICNetProcessor.cc QUICNetVConnection.cc QUICPacketHandler.cc)
    string(APPEND DOXYGEN_EXCLUDE " \"${CMAKE_SOURCE_DIR}/src/iocore/net/${source}\"")
  endforeach()
endif()

configure_file(${CMAKE_SOURCE_DIR}/doc/Doxyfile.in ${CMAKE_BINARY_DIR}/doc/Doxyfile @ONLY)
add_custom_target(
  doxygen
  COMMAND Doxygen::doxygen ${CMAKE_BINARY_DIR}/doc/Doxyfile
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/doc
  COMMENT "Building Doxygen API documentation"
  VERBATIM
)
