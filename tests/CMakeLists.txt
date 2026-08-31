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

include(add_atsplugin)

function(ADD_URTEST_PLUGIN _NAME)
  add_library(${_NAME} MODULE ${ARGN})
  if(LINK_PLUGINS AND BUILD_SHARED_LIBS)
    target_link_libraries(${_NAME} PRIVATE ts::tsapi ts::tsutil)
  else()
    target_include_directories(
      ${_NAME} PRIVATE "$<TARGET_PROPERTY:libswoc::libswoc,INCLUDE_DIRECTORIES>"
                       "$<TARGET_PROPERTY:yaml-cpp::yaml-cpp,INCLUDE_DIRECTORIES>"
    )
  endif()
  set_target_properties(
    ${_NAME}
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.libs"
               PREFIX ""
               SUFFIX ".so"
  )
endfunction()

add_subdirectory(tools/plugins)
add_subdirectory(uranium_tests/chunked_encoding)
add_subdirectory(uranium_tests/at_headers/plugins)
add_subdirectory(uranium_tests/continuations/plugins)
add_subdirectory(uranium_tests/jsonrpc/plugins)
add_subdirectory(uranium_tests/pluginTest/crash_test)
add_subdirectory(uranium_tests/pluginTest/polite_hook_wait)
add_subdirectory(uranium_tests/pluginTest/tsapi)
add_subdirectory(uranium_tests/pluginTest/TSVConnFd)
add_subdirectory(uranium_tests/timeout)
add_subdirectory(uranium_tests/tls)

set(CURL_UDS_FLAG "")
if(ENABLE_URTEST_UDS)
  set(CURL_UDS_FLAG "--curl-uds")
endif()

configure_file(pyproject.toml pyproject.toml COPYONLY)
configure_file(uv.lock uv.lock COPYONLY)
configure_file(urtest.sh.in urtest.sh @ONLY USE_SOURCE_PERMISSIONS)

add_custom_target(
  urtest-replay
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/urtest.sh --no-run-in-container -m uranium_replay
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)

add_custom_target(
  urtest-replay-no-install
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/urtest.sh --no-run-in-container -m uranium_replay
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)

add_custom_target(
  urtest
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/urtest.sh --no-run-in-container
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)

add_custom_target(
  urtest-no-install
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/urtest.sh --no-run-in-container
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)

add_custom_target(
  urtest-uds
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/urtest.sh --no-run-in-container --curl-uds
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)
