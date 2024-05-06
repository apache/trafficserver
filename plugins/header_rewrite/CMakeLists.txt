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

add_atsplugin(
  header_rewrite
  condition.cc
  conditions.cc
  factory.cc
  header_rewrite.cc
  lulu.cc
  matcher.cc
  operator.cc
  operators.cc
  parser.cc
  regex_helper.cc
  resources.cc
  ruleset.cc
  statement.cc
  value.cc
)

add_library(header_rewrite_parser STATIC parser.cc)
target_link_libraries(header_rewrite_parser PUBLIC libswoc::libswoc)

target_link_libraries(
  header_rewrite
  PRIVATE PCRE::PCRE
  PUBLIC libswoc::libswoc
)

if(maxminddb_FOUND)
  target_compile_definitions(header_rewrite PUBLIC TS_USE_HRW_MAXMINDDB=1)
  target_sources(header_rewrite PRIVATE conditions_geo_maxmind.cc)
  target_link_libraries(header_rewrite PRIVATE maxminddb::maxminddb)
endif()

if(BUILD_TESTING)
  add_executable(test_header_rewrite header_rewrite_test.cc)
  add_test(NAME test_header_rewrite COMMAND $<TARGET_FILE:test_header_rewrite>)

  target_link_libraries(test_header_rewrite PRIVATE header_rewrite_parser ts::inkevent ts::tscore)

  if(maxminddb_FOUND)
    target_link_libraries(test_header_rewrite PRIVATE maxminddb::maxminddb)
  endif()
endif()
verify_global_plugin(header_rewrite)
verify_remap_plugin(header_rewrite)
