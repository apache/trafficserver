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
  include(Catch)

  # Disable -Werror=parentheses for Catch2 to avoid issues with older compilers (GCC <=11)
  # that complain about the use of parentheses in macro expansions. We see errors like this:
  #
  # catch_test_macro_impl.hpp:50:67: error: suggest parentheses around comparison in operand of '==' [-Werror=parentheses]
  # ...
  # 297 |       CHECK(fut1.get() == expected1);
  #     |       ^~~~~
  target_compile_options(Catch2 INTERFACE -Wno-error=parentheses)
  target_compile_options(Catch2WithMain INTERFACE -Wno-error=parentheses)

  # Register one ctest test per Catch2 TEST_CASE/SCENARIO rather than one per
  # executable. ctest then names the failing case instead of the binary, can
  # schedule cases in parallel, and can run a single case by name. Because each
  # case gets its own process, cases must not depend on state set up by an
  # earlier case in the same executable.
  #
  # ctest names are "<NAME>.<case name>" so the NAME given here stays a usable
  # -R filter prefix even where it differs from the target name.
  #
  # Discovery is PRE_TEST so that configuring/building does not require running
  # the test executables.
  macro(add_catch2_test)
    cmake_parse_arguments(CATCH2_TEST "" "NAME" "COMMAND;ENVIRONMENT" ${ARGN})

    list(GET CATCH2_TEST_COMMAND 0 _catch2_command)
    list(LENGTH CATCH2_TEST_COMMAND _catch2_command_length)
    if(NOT _catch2_command_length EQUAL 1)
      # Per-case discovery appends the case name to the command line, so there is
      # nowhere to put caller-supplied runner arguments. Fail rather than drop them.
      message(FATAL_ERROR "add_catch2_test(${CATCH2_TEST_NAME}): COMMAND must be a single test executable")
    endif()
    if(_catch2_command MATCHES "^\\$<TARGET_FILE:(.+)>$")
      set(_catch2_target "${CMAKE_MATCH_1}")
    else()
      set(_catch2_target "${_catch2_command}")
    endif()

    if(CATCH2_TEST_ENVIRONMENT)
      # Not ctest's ENVIRONMENT property: catch_discover_tests flattens PROPERTIES
      # into one ;-joined string, so a multi-variable ENVIRONMENT loses every
      # variable but the first and turns the rest into junk property names. The
      # emulator prefix survives intact and also applies to test discovery.
      #
      # Prepend rather than replace: a cross-compiling build may already have an
      # emulator here, and it has to keep running the executable.
      get_property(
        _catch2_emulator
        TARGET ${_catch2_target}
        PROPERTY CROSSCOMPILING_EMULATOR
      )
      set_property(
        TARGET ${_catch2_target} PROPERTY CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND}" -E env ${CATCH2_TEST_ENVIRONMENT}
                                          ${_catch2_emulator}
      )
    endif()

    catch_discover_tests(${_catch2_target} TEST_PREFIX "${CATCH2_TEST_NAME}." DISCOVERY_MODE PRE_TEST)
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

# Currently v1.4.0
#
# Upstream: https://github.com/google/highway
#
# Source-only, preserves the readme, license, cmake
# infrastructure and main source; does not include
# examples, tests or documentation.
#
# Note that hwy/tests/list_targets.cc is retained
# for compiled-in target information
#
# CMakeLists.txt slightly modified to fully disable
# building tests if `HWY_ENABLE_TESTS=OFF`, we intend
# to upstream this change
if(ENABLE_HIGHWAY_DISPATCH AND NOT EXTERNAL_HWY)
  message(STATUS "Using internal highway")
  set(HWY_FORCE_STATIC_LIBS
      ON
      CACHE BOOL "Ignore BUILD_SHARED_LIBS" FORCE
  )
  set(HWY_ENABLE_INSTALL
      OFF
      CACHE BOOL "Install library" FORCE
  )
  set(HWY_ENABLE_EXAMPLES
      OFF
      CACHE BOOL "Build examples" FORCE
  )
  set(HWY_ENABLE_TESTS
      OFF
      CACHE BOOL "Enable HWY tests" FORCE
  )
  set(CMAKE_POLICY_DEFAULT_CMP0075 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  add_subdirectory(highway)
  add_library(hwy::hwy ALIAS hwy)
  add_library(hwy::hwy_contrib ALIAS hwy_contrib)
endif()
