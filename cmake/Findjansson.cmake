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

# Findjansson.cmake
#
# This will define the following variables
#
#     jansson_FOUND
#     jansson_LIBRARY
#     jansson_INCLUDE_DIRS
#
# and the following imported targets
#
#     jansson::jansson
#

find_library(jansson_LIBRARY NAMES jansson)
find_path(jansson_INCLUDE_DIR NAMES jansson.h)

mark_as_advanced(jansson_FOUND jansson_LIBRARY jansson_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(jansson REQUIRED_VARS jansson_LIBRARY jansson_INCLUDE_DIR)

if(jansson_FOUND)
  set(jansson_INCLUDE_DIRS ${jansson_INCLUDE_DIR})
endif()

if(jansson_FOUND AND NOT TARGET jansson::jansson)
  add_library(jansson::jansson INTERFACE IMPORTED)
  target_include_directories(jansson::jansson INTERFACE ${jansson_INCLUDE_DIRS})
  target_link_libraries(jansson::jansson INTERFACE "${jansson_LIBRARY}")
endif()
