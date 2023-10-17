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
  add_test(NAME ${NAME}_esi COMMAND $<TARGET_FILE:${NAME}>)
endmacro()

add_esi_test(test_docnode docnode_test.cc)
target_link_libraries(test_docnode PRIVATE catch2::catch2 esi-common esicore)
add_esi_test(test_parser parser_test.cc)
target_link_libraries(test_parser PRIVATE catch2::catch2 esi-common esicore)
add_esi_test(test_processor processor_test.cc)
target_link_libraries(test_processor PRIVATE catch2::catch2 esi-common esicore)
add_esi_test(test_utils utils_test.cc)
target_link_libraries(test_utils PRIVATE catch2::catch2 esi-common)
add_esi_test(test_vars vars_test.cc)
target_link_libraries(test_vars PRIVATE catch2::catch2 esi-common esicore)
add_esi_test(test_gzip gzip_test.cc)
target_link_libraries(test_gzip PRIVATE catch2::catch2 esi-common)
