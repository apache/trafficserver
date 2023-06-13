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

# Findmimalloc.cmake
#
# This will define the following variables
#
#     mimalloc_FOUND
#     mimalloc_LIBRARY
#     mimalloc_INCLUDE_DIRS
#
# and the following imported targets
#
#     mimalloc::mimalloc
#

find_library(mimalloc_LIBRARY NAMES mimalloc)
find_path(mimalloc_INCLUDE_DIR NAMES mimalloc.h PATH_SUFFIXES mimalloc)

mark_as_advanced(mimalloc_FOUND mimalloc_LIBRARY mimalloc_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(mimalloc
    REQUIRED_VARS mimalloc_LIBRARY mimalloc_INCLUDE_DIR
)

if(mimalloc_FOUND)
    set(mimalloc_INCLUDE_DIRS "${mimalloc_INCLUDE_DIR}")
endif()

if(mimalloc_FOUND AND NOT TARGET mimalloc::mimalloc)
    add_library(mimalloc::mimalloc INTERFACE IMPORTED)
    target_include_directories(mimalloc::mimalloc INTERFACE ${mimalloc_INCLUDE_DIRS})
    target_link_libraries(mimalloc::mimalloc INTERFACE "${mimalloc_LIBRARY}")
endif()
