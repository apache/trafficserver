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

project(slice)

add_atsplugin(
  slice
  Config.cc
  ContentRange.cc
  HttpHeader.cc
  Range.cc
  client.cc
  intercept.cc
  prefetch.cc
  response.cc
  server.cc
  slice.cc
  transfer.cc
  util.cc
)

target_link_libraries(slice PRIVATE PCRE::PCRE)
if(BUILD_TESTING)
  add_subdirectory(unit-tests)
endif()
verify_global_plugin(slice)
verify_remap_plugin(slice)
