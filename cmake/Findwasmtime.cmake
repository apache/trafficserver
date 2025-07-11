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

# Findwasmtime.cmake
#
# This will define the following variables
#
#     wasmtime_FOUND
#     wasmtime_LIBRARY
#     wasmtime_INCLUDE_DIR
#
# and the following imported targets
#
#     wasmtime::wasmtime
#

find_library(lwasmtime_LIBRARY NAMES wasmtime)
find_path(wasmtime_INCLUDE_DIR NAMES crates/c-api/include/wasm.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(wasmtime REQUIRED_VARS lwasmtime_LIBRARY wasmtime_INCLUDE_DIR)

if(wasmtime_FOUND)
  mark_as_advanced(wasmtime_FOUND wasmtime_LIBRARY)
  set(wasmtime_INCLUDE_DIRS ${wasmtime_INCLUDE_DIR})
  set(wasmtime_LIBRARY ${lwasmtime_LIBRARY})
endif()

if(wasmtime_FOUND AND NOT TARGET wasmtime::wasmtime)
  add_library(wasmtime::wasmtime INTERFACE IMPORTED)
  target_include_directories(wasmtime::wasmtime INTERFACE ${wasmtime_INCLUDE_DIRS})
  target_link_libraries(wasmtime::wasmtime INTERFACE ${wasmtime_LIBRARY})
endif()
