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

# Findwamr.cmake
#
# This will define the following variables
#
#     wamr_FOUND
#     wamr_LIBRARY
#     wamr_INCLUDE_DIR
#
# and the following imported targets
#
#     wamr::wamr
#

find_library(iwasm_LIBRARY NAMES iwasm)
find_path(wamr_INCLUDE_DIR NAMES wasm_c_api.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(wamr REQUIRED_VARS iwasm_LIBRARY wamr_INCLUDE_DIR)

if(wamr_FOUND)
  mark_as_advanced(wamr_FOUND wamr_LIBRARY)
  set(wamr_INCLUDE_DIRS ${wamr_INCLUDE_DIR})
  set(wamr_LIBRARY ${iwasm_LIBRARY})
endif()

if(wamr_FOUND AND NOT TARGET wamr::wamr)
  add_library(wamr::wamr INTERFACE IMPORTED)
  target_include_directories(wamr::wamr INTERFACE ${wamr_INCLUDE_DIRS})
  target_link_libraries(wamr::wamr INTERFACE ${wamr_LIBRARY})
endif()
