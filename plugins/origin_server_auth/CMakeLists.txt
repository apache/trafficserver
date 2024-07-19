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

project(origin_server_auth)

add_atsplugin(origin_server_auth origin_server_auth.cc aws_auth_v4.cc)

target_link_libraries(origin_server_auth PRIVATE OpenSSL::Crypto)

verify_remap_plugin(origin_server_auth)

if(BUILD_TESTING)
  add_subdirectory(unit_tests)
endif()

# This allows s3_auth.so to be used
# TODO: s3_auth is deprecated since 10.x. Remove this on a future release.

project(s3_auth)

add_atsplugin(s3_auth origin_server_auth.cc aws_auth_v4.cc)

target_link_libraries(s3_auth PRIVATE OpenSSL::Crypto)

verify_remap_plugin(s3_auth)
