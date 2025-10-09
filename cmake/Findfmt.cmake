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

# Findfmt.cmake
#
# This will define the following variables
#
#     fmt_FOUND
#     fmt_LIBRARY
#     fmt_INCLUDE_DIRS
#
# and the following imported targets
#
#     fmt::fmt
#

find_package(PkgConfig REQUIRED)
pkg_check_modules(fmt fmt)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  fmt
  REQUIRED_VARS fmt_INCLUDE_DIRS fmt_LINK_LIBRARIES fmt_LIBRARIES fmt_LIBRARY_DIRS
  HANDLE_COMPONENTS
)

if(fmt_FOUND)
  set(fmt_INCLUDE_DIRS ${fmt_INCLUDE_DIR})
endif()

if(fmt_FOUND AND NOT TARGET fmt::fmt)
  add_library(fmt::fmt INTERFACE IMPORTED)
  target_include_directories(fmt::fmt INTERFACE ${fmt_INCLUDE_DIRS})
  target_link_directories(fmt::fmt INTERFACE ${fmt_LIBRARY_DIRS})
  target_link_libraries(fmt::fmt INTERFACE ${fmt_LIBRARIES})
endif()
