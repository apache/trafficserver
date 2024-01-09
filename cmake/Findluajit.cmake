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

# Findluajit.cmake
#
# This will define the following variables
#
#     luajit_FOUND
#
# and the following imported targets
#
#     luajit::luajit
#

find_package(PkgConfig REQUIRED)
pkg_check_modules(luajit luajit)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  luajit
  REQUIRED_VARS luajit_INCLUDE_DIRS luajit_LINK_LIBRARIES luajit_LIBRARIES
  HANDLE_COMPONENTS
)

if(luajit_FOUND)
  set(luajit_INCLUDE_DIRS ${luajit_INCLUDE_DIR})
endif()

if(luajit_FOUND AND NOT TARGET luajit::luajit)
  add_library(luajit::luajit INTERFACE IMPORTED)
  target_include_directories(luajit::luajit INTERFACE ${luajit_INCLUDE_DIRS})
  if(luajit_LIBRARY_DIRS)
    target_link_directories(luajit::luajit INTERFACE ${luajit_LIBRARY_DIRS})
  endif()
  target_link_libraries(luajit::luajit INTERFACE ${luajit_LIBRARIES})
endif()
