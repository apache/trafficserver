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

# Findmaxminddb.cmake
#
# This will define the following variables
#
#     maxminddb_FOUND
#     maxminddb_LIBRARY
#     maxminddb_INCLUDE_DIRS
#
# and the following imported targets
#
#     maxminddb::maxminddb
#

# maxminddb exports their own config since maxminddb-1.5.0, but it isn't
# present in the OpenSUSE libmaxminddb-devel-1.7.1 package and maybe others.

find_library(maxminddb_LIBRARY NAMES maxminddb)
find_path(maxminddb_INCLUDE_DIR NAMES maxminddb.h)

mark_as_advanced(maxminddb_FOUND maxminddb_LIBRARY maxminddb_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(maxminddb REQUIRED_VARS maxminddb_LIBRARY maxminddb_INCLUDE_DIR)

if(maxminddb_FOUND)
  set(maxminddb_INCLUDE_DIRS ${maxminddb_INCLUDE_DIR})
endif()

if(maxminddb_FOUND AND NOT TARGET maxminddb::maxminddb)
  add_library(maxminddb::maxminddb INTERFACE IMPORTED)
  target_include_directories(maxminddb::maxminddb INTERFACE ${maxminddb_INCLUDE_DIRS})
  target_link_libraries(maxminddb::maxminddb INTERFACE "${maxminddb_LIBRARY}")
endif()
