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
project(txn_box)
set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)
# include(FindOpenSSL)
# include(FetchContent)

# set(INSTALL_DIR ${CMAKE_HOME_DIRECTORY})
# set(CMAKE_PREFIX_PATH ${INSTALL_DIR})
# if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
#     set(CMAKE_INSTALL_PREFIX "${CMAKE_HOME_DIRECTORY}" CACHE PATH "..." FORCE)
# endif()

find_package(PkgConfig REQUIRED)

# FetchContent_Declare(
#     libSWOC
#     GIT_REPOSITORY "https://github.com/solidwallofcode/libswoc"
#     GIT_TAG "1.5.4"
# )
# set(LIBSWOC_INSTALL off)
# set(LIBSWOC_TEST off)
# FetchContent_MakeAvailable(libSWOC)

# pkg_check_modules(trafficserver REQUIRED IMPORTED_TARGET trafficserver)

add_subdirectory(plugin)

if(BUILD_TESTING)
  add_subdirectory(unit_tests)
endif()
#add_subdirectory(test/unit_tests)
