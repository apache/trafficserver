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

# Findnuma.cmake
#
# This will define the following variables
#
#     numa_FOUND
#     numa_LIBRARY
#     numa_INCLUDE_DIRS
#
# and the following imported targets
#
#     numa::numa
#

find_library(numa_LIBRARY NAMES numa)
find_path(numa_INCLUDE_DIR NAMES numa.h)

mark_as_advanced(numa_FOUND numa_LIBRARY numa_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(numa REQUIRED_VARS numa_LIBRARY numa_INCLUDE_DIR)

if(numa_FOUND)
  set(numa_INCLUDE_DIRS ${numa_INCLUDE_DIR})
endif()

if(numa_FOUND AND NOT TARGET numa::numa)
  add_library(numa::numa INTERFACE IMPORTED)
  target_include_directories(numa::numa INTERFACE ${numa_INCLUDE_DIRS})
  target_link_libraries(numa::numa INTERFACE ${numa_LIBRARY})
endif()
