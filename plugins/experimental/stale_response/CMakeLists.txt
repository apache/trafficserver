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

project(stale_response)

add_atsplugin(
  stale_response
  DirectiveParser.cc
  CacheUpdate.cc
  MurmurHash3.cc
  NumberToString.cc
  ServerIntercept.cc
  stale_response.cc
)

target_link_libraries(stale_response PRIVATE libswoc::libswoc)
target_include_directories(stale_response PRIVATE "${libswoc_INCLUDE_DIRS}")

if(BUILD_TESTING)
  add_subdirectory(unit_tests)
endif()
verify_global_plugin(stale_response)
verify_remap_plugin(stale_response)
