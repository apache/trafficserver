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

# FindTCMalloc.cmake
#
# This will define the following variables
#
#     TCMalloc_FOUND
#     TCMalloc_LIBRARY
#
# and the following imported targets
#
#     TCMalloc::TCMalloc
#

# libtcmalloc.so symlink not created on OpenSUSE Leap 15.4
find_library(TCMalloc_LIBRARY NAMES libtcmalloc libtcmalloc.so.4 PATHS)
message(STATUS ${TCMalloc_LIBRARY})

mark_as_advanced(TCMalloc_FOUND TCMalloc_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TCMalloc REQUIRED_VARS TCMalloc_LIBRARY)

if(TCMalloc_FOUND AND NOT TARGET TCMalloc::TCMalloc)
    add_library(TCMalloc::TCMalloc INTERFACE IMPORTED)
    target_link_libraries(TCMalloc::TCMalloc INTERFACE "${TCMalloc_LIBRARY}")
endif()
