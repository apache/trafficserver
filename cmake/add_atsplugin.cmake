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
  target_link_libraries(${name} PRIVATE ts::tsapi ts::tsutil)
  set_target_properties(${name} PROPERTIES PREFIX "")
  set_target_properties(${name} PROPERTIES SUFFIX ".so")
  install(TARGETS ${name} DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
endfunction()

function(verify_remap_plugin target)
  add_test(NAME verify_${target} COMMAND $<TARGET_FILE:traffic_server> -C
                                         "verify_remap_plugin $<TARGET_FILE:${target}>"
  )
endfunction()

function(verify_global_plugin target)
  add_test(NAME verify_${target} COMMAND $<TARGET_FILE:traffic_server> -C
                                         "verify_global_plugin $<TARGET_FILE:${target}>"
  )
endfunction()

if(APPLE)
  set(CMAKE_MODULE_LINKER_FLAGS "-undefined dynamic_lookup")
endif()
