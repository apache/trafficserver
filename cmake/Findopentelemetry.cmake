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

# Findopentelemetry.cmake
#
# This will define the following variables
#
#     opentelemetry_FOUND
#     opentelemetry_LIBRARY
#     opentelemetry_INCLUDE_DIRS
#
# and the following imported targets
#
#     opentelemetry::opentelemetry
#

find_package(PkgConfig)
if(opentelemetry_ROOT)
  list(APPEND CMAKE_PREFIX_PATH ${opentelemetry_ROOT})
endif()
pkg_check_modules(opentelemetry_api opentelemetry_api)

mark_as_advanced(opentelemetry_api_FOUND opentelemetry_api_LIBRARY opentelemetry_api_INCLUDE_DIR)

if(opentelemetry_api_FOUND)
  set(opentelemetry_INCLUDE_DIRS ${opentelemetry_api_INCLUDE_DIR})
endif()

if(opentelemetry_api_FOUND AND NOT TARGET opentelemetry::opentelemetry)
  add_library(opentelemetry::opentelemetry INTERFACE IMPORTED)
  target_include_directories(opentelemetry::opentelemetry INTERFACE ${opentelemetry_api_INCLUDE_DIRS})
  target_link_libraries(opentelemetry::opentelemetry INTERFACE ${opentelemetry_api_LIBRARY})
  set(opentelemetry_FOUND true)
endif()
