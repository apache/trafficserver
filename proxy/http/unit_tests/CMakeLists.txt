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

add_executable(test_http test_ForwardedConfig.cc test_PreWarm.cc)

# transitive
target_include_directories(test_http PRIVATE "${PROJECT_SOURCE_DIR}/mgmt")

target_link_libraries(test_http
    PRIVATE
        catch2::catch2
        ts::http
        ts::inkevent # transitive
        ts::proxy # transitive
)
if(TS_USE_POSIX_CAP)
    target_link_libraries(test_http PUBLIC cap::cap)
endif()

add_test(NAME test_http COMMAND $<TARGET_FILE:test_http>)
