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

set(CMAKE_SHARED_LIBRARY_PREFIX "")

function(add_atsplugin name)
  add_library(${name} MODULE ${ARGN})
  target_link_libraries(${name} PRIVATE traffic_server)
  set_target_properties(${name} PROPERTIES PREFIX "")
  set_target_properties(${name} PROPERTIES SUFFIX ".so")
  set_target_properties(${name} PROPERTIES LINK_FLAGS "-Wl,-rpath,${CMAKE_INSTALL_PREFIX}/lib")
  install(TARGETS ${name} DESTINATION libexec/trafficserver)
endfunction()

if(APPLE)
  set(CMAKE_MODULE_LINKER_FLAGS "-undefined dynamic_lookup")
endif()

add_subdirectory(authproxy)
add_subdirectory(background_fetch)
add_subdirectory(cache_promote)
add_subdirectory(cache_range_requests)
add_subdirectory(cachekey)
add_subdirectory(certifier)
add_subdirectory(compress)
add_subdirectory(conf_remap)
add_subdirectory(escalate)
add_subdirectory(esi)

add_subdirectory(generator)
add_subdirectory(header_rewrite)
add_subdirectory(libloader)
add_subdirectory(multiplexer)
add_subdirectory(prefetch)
add_subdirectory(remap_purge)
add_subdirectory(regex_remap)
add_subdirectory(s3_auth)
add_subdirectory(xdebug)

if(HOST_OS STREQUAL "linux")
    add_subdirectory(healthchecks)
endif()
