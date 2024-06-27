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

function(cripts_sources)
  target_sources(cripts PUBLIC ${ARGN})
endfunction()

function(cripts_include_directories)
  foreach(DIR ${ARGN})
    target_include_directories(cripts AFTER PUBLIC $<BUILD_INTERFACE:${DIR}>)
  endforeach()
endfunction()

function(cripts_link_libraries)
  target_link_libraries(cripts PUBLIC ${ARGN})
endfunction()

function(cripts_install_bundle_headers bundle)
  install(FILES ${ARGN} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cripts/${bundle})
endfunction()
