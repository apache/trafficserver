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

# Findcjose.cmake
#
# This will define the following variables
#
#     cjose_FOUND
#     cjose_LIBRARY
#     cjose_INCLUDE_DIRS
#
# and the following imported targets
#
#     cjose::cjose
#

find_library(cjose_LIBRARY NAMES cjose)
find_path(cjose_INCLUDE_DIR NAMES cjose/cjose.h)

mark_as_advanced(cjose_FOUND cjose_LIBRARY cjose_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cjose REQUIRED_VARS cjose_LIBRARY cjose_INCLUDE_DIR)

if(cjose_FOUND)
  set(cjose_INCLUDE_DIRS ${cjose_INCLUDE_DIR})
endif()

if(cjose_FOUND AND NOT TARGET cjose::cjose)
  add_library(cjose::cjose INTERFACE IMPORTED)
  target_include_directories(cjose::cjose INTERFACE ${cjose_INCLUDE_DIRS})
  target_link_libraries(cjose::cjose INTERFACE "${cjose_LIBRARY}")
endif()
