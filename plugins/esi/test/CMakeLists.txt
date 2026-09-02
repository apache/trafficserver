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

add_library(esitest STATIC HandlerMap.cc StubIncludeHandler.cc TestHandlerManager.cc)
target_link_libraries(esitest PUBLIC esi-common esicore)

macro(ADD_ESI_TEST NAME)
  add_executable(${NAME} print_funcs.cc ${ARGN})
  target_link_libraries(${NAME} PRIVATE esitest)
  add_catch2_test(NAME ${NAME}_esi COMMAND $<TARGET_FILE:${NAME}>)
  # Use ASan leak suppression for intentional DbgCtl leaks (issue #12776)
  set_tests_properties(
    ${NAME}_esi PROPERTIES ENVIRONMENT
                           "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/esi_test_leak_suppression.txt"
  )
endmacro()

# esicore does not link ts::tsutil (see plugins/esi/lib/CMakeLists.txt for why), so test
# executables that pull in tsutil symbols transitively via esicore must link tsutil
# themselves. Plugins (esi.so, combo_handler.so) don't need this because they resolve
# tsutil symbols dynamically against traffic_server at dlopen time.
add_esi_test(test_docnode docnode_test.cc)
target_link_libraries(test_docnode PRIVATE Catch2::Catch2WithMain esi-common esicore ts::tsutil)
add_esi_test(test_parser parser_test.cc)
target_link_libraries(test_parser PRIVATE Catch2::Catch2WithMain esi-common esicore ts::tsutil)
add_esi_test(test_processor processor_test.cc)
target_link_libraries(test_processor PRIVATE Catch2::Catch2WithMain esi-common esicore ts::tsutil)
add_esi_test(test_utils utils_test.cc)
target_link_libraries(test_utils PRIVATE Catch2::Catch2WithMain esi-common)
add_esi_test(test_vars vars_test.cc)
target_link_libraries(test_vars PRIVATE Catch2::Catch2WithMain esi-common esicore ts::tsutil)
add_esi_test(test_gzip gzip_test.cc)
target_link_libraries(test_gzip PRIVATE Catch2::Catch2WithMain esi-common)
add_esi_test(test_include_url_validator include_url_validator_test.cc)
target_link_libraries(test_include_url_validator PRIVATE Catch2::Catch2WithMain esi-common esicore ts::tsutil)
# combo_handler_utils.cc lives in the parent plugin source dir; compile
# it directly into the test binary since it is bundled into combo_handler.so
# rather than its own library.
add_esi_test(test_combo_handler_utils combo_handler_utils_test.cc ${CMAKE_CURRENT_SOURCE_DIR}/../combo_handler_utils.cc)
target_include_directories(test_combo_handler_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_combo_handler_utils PRIVATE Catch2::Catch2WithMain)
