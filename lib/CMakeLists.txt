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

if(BUILD_TESTING)
  # Currently v3.9.1. To update:
  # cd <git-clone-root>/lib
  # rm -rf Catch2
  # git clone https://github.com/catchorg/Catch2.git
  # cd Catch2
  # git checkout <desired-version>
  # rm -rf .git data examples docs fuzzing tests tools .bazelrc .conan .github
  # cd ..
  # git add Catch2
  #
  # Update the above comment about the version to update.
  add_subdirectory(Catch2)

  # Disable -Werror=parentheses for Catch2 to avoid issues with older compilers (GCC <=11)
  # that complain about the use of parentheses in macro expansions. We see errors like this:
  #
  # catch_test_macro_impl.hpp:50:67: error: suggest parentheses around comparison in operand of '==' [-Werror=parentheses]
  # ...
  # 297 |       CHECK(fut1.get() == expected1);
  #     |       ^~~~~
  target_compile_options(Catch2 INTERFACE -Wno-error=parentheses)
  target_compile_options(Catch2WithMain INTERFACE -Wno-error=parentheses)

  # By default, as of v3.9.0, Catch2 runs tests in random order. Our tests are
  # written with the expectation that they are run in declared order, with setup
  # done in one TEST_CASE expected to carry over into the next TEST_CASE. This
  # macro allows us to add common command line arguments to all tests. For now,
  # we ensure declaration order execution via --order decl.
  macro(add_catch2_test)
    cmake_parse_arguments(CATCH2_TEST "" "NAME" "COMMAND" ${ARGN})
    add_test(NAME ${CATCH2_TEST_NAME} COMMAND ${CATCH2_TEST_COMMAND} --order decl)
  endmacro()
endif()

if(NOT TARGET yaml-cpp::yaml-cpp)
  # Turning CLANG_FORMAT off causes yamlcpp to not add its format target which
  # otherwise conflicts with our format target.
  set(YAML_CPP_CLANG_FORMAT_EXE OFF)
  set(YAML_CPP_INSTALL ON)
  add_subdirectory(yamlcpp)
  install(TARGETS yaml-cpp)
endif()

add_subdirectory(fastlz)

if(NOT EXTERNAL_LIBSWOC)
  message(STATUS "Using internal libswoc")
  add_subdirectory(swoc)
  set(SWOC_INCLUDE_DIR
      ${CMAKE_SOURCE_DIR}/lib/swoc/include
      PARENT_SCOPE
  )
endif()

add_library(systemtap::systemtap INTERFACE IMPORTED GLOBAL)
target_include_directories(systemtap::systemtap INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/systemtap")

add_subdirectory(ls-hpack)
