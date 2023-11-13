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

# FindLuaJIT.cmake
#
# This will define the following variables
#
#     LuaJIT_FOUND
#
# and the following imported targets
#
#     LuaJIT::LuaJIT
#

# LuaJIT exports their own config since LuaJIT-1.5.0, but it isn't
# present in the OpenSUSE libLuaJIT-devel-1.7.1 package and maybe others.

find_package(PkgConfig REQUIRED)
pkg_check_modules(LuaJIT luajit)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LuaJIT REQUIRED_VARS LuaJIT_LIBRARIES LuaJIT_INCLUDE_DIRS)

if(LuaJIT_FOUND)
  set(LuaJIT_INCLUDE_DIRS ${LuaJIT_INCLUDE_DIR})
endif()

if(LuaJIT_FOUND AND NOT TARGET LuaJIT::LuaJIT)
  add_library(LuaJIT::LuaJIT INTERFACE IMPORTED)
  target_include_directories(LuaJIT::LuaJIT INTERFACE ${LuaJIT_INCLUDE_DIRS})
  target_link_directories(LuaJIT::LuaJIT INTERFACE ${LuaJIT_LIBRARY_DIRS})
  target_link_libraries(LuaJIT::LuaJIT INTERFACE ${LuaJIT_LIBRARIES})
endif()
