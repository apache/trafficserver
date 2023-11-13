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

# Findhwloc.cmake
#
# This will define the following variables
#
#     hwloc_FOUND
#     hwloc_LIBRARY
#     hwloc_INCLUDE_DIRS
#
# and the following imported targets
#
#     hwloc::hwloc
#

find_library(hwloc_LIBRARY NAMES hwloc)
find_path(hwloc_INCLUDE_DIR NAMES hwloc.h)

mark_as_advanced(hwloc_FOUND hwloc_LIBRARY hwloc_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hwloc REQUIRED_VARS hwloc_LIBRARY hwloc_INCLUDE_DIR)

if(hwloc_FOUND)
  set(hwloc_INCLUDE_DIRS ${hwloc_INCLUDE_DIR})
endif()

if(hwloc_FOUND AND NOT TARGET hwloc::hwloc)
  add_library(hwloc::hwloc INTERFACE IMPORTED)
  target_include_directories(hwloc::hwloc INTERFACE ${hwloc_INCLUDE_DIRS})
  target_link_libraries(hwloc::hwloc INTERFACE ${hwloc_LIBRARY})
endif()
