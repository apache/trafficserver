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

find_package(NuRaft QUIET)

if(NuRaft_FOUND)
  add_atsplugin(stek_share common.cc log_store.cc stek_share.cc stek_utils.cc)

  target_link_libraries(stek_share PRIVATE NuRaft::static_lib OpenSSL::SSL yaml-cpp::yaml-cpp)
else()
  message(STATUS "skipping stek_share plugin (missing NuRaft)")
endif()
