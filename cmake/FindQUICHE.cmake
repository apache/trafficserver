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

set(QUICHE_INSTALL_DIR /opt/quiche)

find_path(QUICHE_INCLUDE_DIR NAMES quiche.h HINTS ${QUICHE_INSTALL_DIR} PATH_SUFFIXES include)
find_library(QUICHE_LIBRARY NAMES quiche HINTS ${QUICHE_INSTALL_DIR} PATH_SUFFIXES lib)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(QUICHE DEFAULT_MSG QUICHE_LIBRARY QUICHE_INCLUDE_DIR)

if(QUICHE_FOUND)
  set(QUICHE_LIBRARY_DIR ${QUICHE_INSTALL_DIR}/lib)
  set(QUIC_LIBRARY_DIRS ${QUICHE_LIBRARY_DIR})
  set(QUIC_LIBRARIES ${QUICHE_LIBRARY} http3 quic)
  set(QUIC_INCLUDE_DIRS ${QUICHE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/iocore/net/quic)
  set(TS_HAS_QUICHE 1)
  set(TS_USE_QUIC 1)
endif()

unset(QUICHE_INSTALL_DIR)
