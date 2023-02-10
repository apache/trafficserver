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



add_library(mgmt_c STATIC RecordsConfig.cc)
add_library(mgmt_p STATIC
        BaseManager.cc
        RecordsConfigUtils.cc
        RecordsConfig.cc
        ProcessManager.cc
        ProxyConfig.cc
        YamlCfg.cc
)
add_library(mgmt_lm STATIC
        BaseManager.cc
        RecordsConfigUtils.cc
        Alarms.cc
        DerivativeMetrics.cc
        FileManager.cc
        LocalManager.cc
        ConfigManager.cc
        WebMgmtUtils.cc
)
include_directories(${IOCORE_INCLUDE_DIRS} ${PROXY_INCLUDE_DIRS} ${YAML_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/utils ${CMAKE_CURRENT_SOURCE_DIR})

#target_link_libraries(mgmt_lm yaml-cpp proxy logging tscore records_lm mgmt_c inkevent utils_lm proxy)
#target_link_libraries(mgmt_p yaml-cpp proxy logging tscore records_p mgmt_c inkevent utils_p )

add_subdirectory(utils)