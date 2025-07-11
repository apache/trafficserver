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

# Findwasmedge.cmake
#
# This will define the following variables
#
#     wasmedge_FOUND
#     wasmedge_LIBRARY
#     wasmedge_INCLUDE_DIR
#
# and the following imported targets
#
#     wasmedge::wasmedge
#

find_library(lwasmedge_LIBRARY NAMES wasmedge)
find_path(wasmedge_INCLUDE_DIR NAMES wasmedge/wasmedge.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(wasmedge REQUIRED_VARS lwasmedge_LIBRARY wasmedge_INCLUDE_DIR)

if(wasmedge_FOUND)
  mark_as_advanced(wasmedge_FOUND wasmedge_LIBRARY)
  set(wasmedge_INCLUDE_DIRS ${wasmedge_INCLUDE_DIR})
  set(wasmedge_LIBRARY ${lwasmedge_LIBRARY})
endif()

if(wasmedge_FOUND AND NOT TARGET wasmedge::wasmedge)
  add_library(wasmedge::wasmedge INTERFACE IMPORTED)
  target_include_directories(wasmedge::wasmedge INTERFACE ${wasmedge_INCLUDE_DIRS})
  target_link_libraries(wasmedge::wasmedge INTERFACE ${wasmedge_LIBRARY})
endif()
