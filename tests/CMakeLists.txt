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

function(ADD_AUTEST_PLUGIN _NAME)
  add_library(${_NAME} MODULE ${ARGN})
  set_target_properties(
    ${_NAME}
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.libs"
               PREFIX ""
               SUFFIX ".so"
  )
  target_link_libraries(${_NAME} PRIVATE ts::tsapi)
endfunction()

add_subdirectory(tools/plugins)
add_subdirectory(gold_tests/bigobj)
add_subdirectory(gold_tests/chunked_encoding)
add_subdirectory(gold_tests/continuations/plugins)
add_subdirectory(gold_tests/jsonrpc/plugins)
add_subdirectory(gold_tests/pluginTest/polite_hook_wait)
add_subdirectory(gold_tests/pluginTest/tsapi)
add_subdirectory(gold_tests/pluginTest/TSVConnFd)
add_subdirectory(gold_tests/timeout)
add_subdirectory(gold_tests/tls)

set(RUNPIPENV PIPENV_VENV_IN_PROJECT=True ${PipEnv})

configure_file(Pipfile Pipfile COPYONLY)
configure_file(autest.sh.in autest.sh)

add_custom_target(
  autest
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install
  COMMAND ${RUNPIPENV} install
  COMMAND ${RUNPIPENV} run env autest --directory ${CMAKE_CURRENT_SOURCE_DIR}/gold_tests
          --ats-bin=${CMAKE_INSTALL_PREFIX}/bin --proxy-verifier-bin ${PROXY_VERIFIER_PATH} --build-root
          ${CMAKE_BINARY_DIR} --sandbox ${AUTEST_SANDBOX}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
)
