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

project(cookie_remap)

add_atsplugin(cookie_remap
    cookie_remap.cc
    cookiejar.cc
    strip.cc
    hash.cc
)

target_link_libraries(cookie_remap
    PRIVATE
        PCRE::PCRE
        yaml-cpp::yaml-cpp
)

if(BUILD_TESTING)
    add_executable(test_cookiejar
        test_cookiejar.cc
        strip.cc
        cookiejar.cc
    )

    target_include_directories(test_cookiejar PRIVATE "${PROJECT_SOURCE_DIR}")

    target_link_libraries(test_cookiejar PRIVATE catch2::catch2)

    add_test(NAME test_cookiejar COMMAND test_cookiejar)
endif()
