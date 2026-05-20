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

# Findquiche.cmake
#
# This will define the following variables
#
#     quiche_FOUND
#     quiche_LIBRARY
#     quiche_INCLUDE_DIRS
#
# and the following imported targets
#
#     quiche::quiche
#

if(quiche_USE_STATIC
   AND quiche_LIBRARY
   AND NOT quiche_LIBRARY MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$"
)
  unset(quiche_LIBRARY CACHE)
endif()

if(quiche_USE_STATIC)
  set(_quiche_ORIGINAL_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

find_library(quiche_LIBRARY NAMES quiche)

if(quiche_USE_STATIC)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${_quiche_ORIGINAL_FIND_LIBRARY_SUFFIXES})
endif()

find_path(
  quiche_INCLUDE_DIR
  NAMES quiche.h
  PATH_SUFFIXES
)

mark_as_advanced(quiche_FOUND quiche_LIBRARY quiche_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(quiche REQUIRED_VARS quiche_LIBRARY quiche_INCLUDE_DIR)

if(quiche_USE_STATIC
   AND quiche_FOUND
   AND NOT quiche_LIBRARY MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$"
)
  message(FATAL_ERROR "Static quiche was requested, but ${quiche_LIBRARY} is not a static library")
endif()

if(quiche_FOUND)
  set(quiche_INCLUDE_DIRS "${quiche_INCLUDE_DIR}")
  if(quiche_USE_STATIC)
    message(STATUS "Using static quiche library: ${quiche_LIBRARY}")
  endif()
endif()

if(quiche_FOUND AND NOT TARGET quiche::quiche)
  add_library(quiche::quiche INTERFACE IMPORTED)
  target_include_directories(quiche::quiche INTERFACE ${quiche_INCLUDE_DIRS})
  target_link_libraries(quiche::quiche INTERFACE "${quiche_LIBRARY}")
endif()
