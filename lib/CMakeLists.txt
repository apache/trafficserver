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



# yamlcpp shadows a bunch of variables but we don't want to hear bout it
add_compile_options(-Wno-shadow)
set(BUILD_SHARED_LIBS 1)

if(NOT TARGET yaml-cpp::yaml-cpp)
  # Turning CLANG_FORMAT off causes yamlcpp to not add its format target which
  # otherwise conflicts with our format target.
  set(YAML_CPP_CLANG_FORMAT_EXE OFF)
  set(YAML_CPP_INSTALL ON)
  add_subdirectory(yamlcpp)
endif()

add_subdirectory(fastlz)
add_subdirectory(swoc)
set(SWOC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/swoc/include PARENT_SCOPE)

add_library(catch2::catch2 INTERFACE IMPORTED GLOBAL)
target_include_directories(catch2::catch2 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/catch2")

install(TARGETS libswoc fastlz)
