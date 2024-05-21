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

### The shared libraries built here are only used by the plugin tests  ####################

function(add_plugin_ut_lib name)
  add_library(${name} MODULE ${ARGN})
  set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.libs" PREFIX "")
  set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.libs" SUFFIX ".so")
  target_link_libraries(${name} PRIVATE ts::tsapi ts::tsutil)
  target_include_directories(
    ${name} PRIVATE "$<TARGET_PROPERTY:libswoc::libswoc,INCLUDE_DIRECTORIES>"
                    "$<TARGET_PROPERTY:libswoc::libswoc,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  remove_definitions(-DATS_BUILD) # remove the ATS_BUILD define for plugins to build without issue
endfunction()

# Test plugins will not build on OSX
#
add_plugin_ut_lib(plugin_v1 plugin_misc_cb.cc)
target_compile_definitions(plugin_v1 PRIVATE PLUGINDSOVER=1)
#
add_plugin_ut_lib(plugin_v2 plugin_misc_cb.cc)
target_compile_definitions(plugin_v2 PRIVATE PLUGINDSOVER=2)
#
add_plugin_ut_lib(plugin_init_fail plugin_init_fail.cc)
#
add_plugin_ut_lib(plugin_instinit_fail plugin_instinit_fail.cc)
#
add_plugin_ut_lib(plugin_required_cb plugin_required_cb.cc)
target_compile_definitions(plugin_required_cb PRIVATE PLUGINDSOVER=1)

add_plugin_ut_lib(plugin_missing_deleteinstance plugin_missing_deleteinstance.cc)
target_compile_definitions(plugin_missing_deleteinstance PRIVATE PLUGINDSOVER=1)

add_plugin_ut_lib(plugin_missing_doremap plugin_missing_doremap.cc)
target_compile_definitions(plugin_missing_doremap PRIVATE PLUGINDSOVER=1)

add_plugin_ut_lib(plugin_missing_init plugin_missing_init.cc)
target_compile_definitions(plugin_missing_init PRIVATE PLUGINDSOVER=1)

add_plugin_ut_lib(plugin_missing_newinstance plugin_missing_newinstance.cc)
target_compile_definitions(plugin_missing_newinstance PRIVATE PLUGINDSOVER=1)

add_plugin_ut_lib(plugin_testing_calls plugin_testing_calls.cc plugin_testing_common.cc)
target_compile_definitions(plugin_testing_calls PRIVATE PLUGINDSOVER=1)

### test_PluginDso ########################################################################

add_executable(test_PluginDso test_PluginDso.cc plugin_testing_common.cc ../PluginDso.cc)

target_compile_definitions(test_PluginDso PRIVATE PLUGIN_DSO_TESTS)

target_include_directories(test_PluginDso PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_PluginDso PRIVATE catch2::catch2 ts::tsapi ts::configmanager ts::inkevent ts::proxy libswoc::libswoc
)

# This test currently does not pass.
add_test(NAME test_PluginDso COMMAND $<TARGET_FILE:test_PluginDso>)

### test_PluginFactory ########################################################################

add_executable(
  test_PluginFactory test_PluginFactory.cc plugin_testing_common.cc ../PluginFactory.cc ../PluginDso.cc
                     ../RemapPluginInfo.cc ${PROJECT_SOURCE_DIR}/src/api/APIHooks.cc
)

target_compile_definitions(test_PluginFactory PRIVATE PLUGIN_DSO_TESTS)

target_include_directories(test_PluginFactory PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_PluginFactory
  PRIVATE catch2::catch2
          tscore
          ts::tsapi
          ts::configmanager
          ts::hdrs
          ts::proxy
          ts::inkdns
          ts::inkutils
          ts::inknet
          ts::inkevent
          libswoc::libswoc
)

# This test currently does not pass.
add_test(NAME test_PluginFactory COMMAND $<TARGET_FILE:test_PluginFactory>)

### test_RemapPluginInfo ########################################################################

add_executable(
  test_RemapPluginInfo test_RemapPlugin.cc plugin_testing_common.cc ../PluginDso.cc ../RemapPluginInfo.cc
                       ${PROJECT_SOURCE_DIR}/src/api/APIHooks.cc
)

target_compile_definitions(test_RemapPluginInfo PRIVATE PLUGIN_DSO_TESTS)

target_include_directories(test_RemapPluginInfo PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_RemapPluginInfo
  PRIVATE catch2::catch2
          tscore
          ts::tsapi
          ts::configmanager
          ts::hdrs
          ts::proxy
          ts::inkdns
          ts::inkutils
          ts::inknet
          ts::inkevent
          libswoc::libswoc
)

# This test currently does not pass.
add_test(NAME test_RemapPluginInfo COMMAND $<TARGET_FILE:test_RemapPluginInfo>)

### test_NextHopStrategyFactory ########################################################################

add_executable(
  test_NextHopStrategyFactory
  test_NextHopStrategyFactory.cc
  nexthop_test_stubs.cc
  ../NextHopSelectionStrategy.cc
  ../NextHopStrategyFactory.cc
  ../NextHopRoundRobin.cc
  ../NextHopConsistentHash.cc
  ../NextHopHealthStatus.cc
  ${PROJECT_SOURCE_DIR}/src/api/APIHooks.cc
)

target_compile_definitions(
  test_NextHopStrategyFactory PRIVATE _NH_UNIT_TESTS_ TS_SRC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_include_directories(test_NextHopStrategyFactory PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_NextHopStrategyFactory PRIVATE catch2::catch2 ts::hdrs ts::inkutils tscore libswoc::libswoc yaml-cpp::yaml-cpp
)

add_test(NAME test_NextHopStrategyFactory COMMAND $<TARGET_FILE:test_NextHopStrategyFactory>)

### test_NextHopRoundRobin ########################################################################

add_executable(
  test_NextHopRoundRobin
  test_NextHopRoundRobin.cc
  nexthop_test_stubs.cc
  ../NextHopSelectionStrategy.cc
  ../NextHopStrategyFactory.cc
  ../NextHopRoundRobin.cc
  ../NextHopConsistentHash.cc
  ../NextHopHealthStatus.cc
  ${PROJECT_SOURCE_DIR}/src/api/APIHooks.cc
)

target_compile_definitions(test_NextHopRoundRobin PRIVATE _NH_UNIT_TESTS_ TS_SRC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

target_include_directories(test_NextHopRoundRobin PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_NextHopRoundRobin
  PRIVATE catch2::catch2
          ts::hdrs
          ts::inkevent
          ts::inkutils
          tscore
          libswoc::libswoc
          yaml-cpp::yaml-cpp
)

add_test(NAME test_NextHopRoundRobin COMMAND $<TARGET_FILE:test_NextHopRoundRobin>)

### test_NextHopConsistentHash ########################################################################

add_executable(
  test_NextHopConsistentHash
  test_NextHopConsistentHash.cc
  nexthop_test_stubs.cc
  ../NextHopSelectionStrategy.cc
  ../NextHopStrategyFactory.cc
  ../NextHopConsistentHash.cc
  ../NextHopRoundRobin.cc
  ../NextHopHealthStatus.cc
  ${PROJECT_SOURCE_DIR}/src/api/APIHooks.cc
)

target_compile_definitions(
  test_NextHopConsistentHash PRIVATE _NH_UNIT_TESTS_ TS_SRC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_include_directories(test_NextHopConsistentHash PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)

target_link_libraries(
  test_NextHopConsistentHash
  PRIVATE catch2::catch2
          tscore
          ts::inkevent
          ts::hdrs
          ts::inkutils
          libswoc::libswoc
          yaml-cpp::yaml-cpp
)

add_test(NAME test_NextHopConsistentHash COMMAND $<TARGET_FILE:test_NextHopConsistentHash>)
