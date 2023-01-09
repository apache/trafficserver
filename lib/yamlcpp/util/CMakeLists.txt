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


add_executable(yaml-cpp-sandbox sandbox.cpp)
add_executable(yaml-cpp-parse parse.cpp)
add_executable(yaml-cpp-read read.cpp)

target_link_libraries(yaml-cpp-sandbox PRIVATE yaml-cpp)
target_link_libraries(yaml-cpp-parse PRIVATE yaml-cpp)
target_link_libraries(yaml-cpp-read PRIVATE yaml-cpp)

set_property(TARGET yaml-cpp-sandbox PROPERTY OUTPUT_NAME sandbox)
set_property(TARGET yaml-cpp-parse PROPERTY OUTPUT_NAME parse)
set_property(TARGET yaml-cpp-read PROPERTY OUTPUT_NAME read)

set_target_properties(yaml-cpp-sandbox
  PROPERTIES
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME sandbox)

set_target_properties(yaml-cpp-parse
  PROPERTIES
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME parse)

set_target_properties(yaml-cpp-read
  PROPERTIES
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME read)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set_target_properties(yaml-cpp-sandbox yaml-cpp-parse yaml-cpp-read
    PROPERTIES
      CXX_STANDARD 11)
endif()
