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

# Function to build pre-compiled cript scripts
function(add_cript name source_file)
  # Check if ENABLE_CRIPTS is ON, if not, skip
  if(NOT ENABLE_CRIPTS)
    message(STATUS "Skipping cript ${name} - ENABLE_CRIPTS is OFF")
    return()
  endif()

  # Use the standard ATS plugin macro and link with cripts
  add_atsplugin(${name} ${source_file})
  target_link_libraries(${name} PRIVATE ts::cripts)

  # Tell CMake that .cript files are C++ files
  set_target_properties(${name} PROPERTIES LINKER_LANGUAGE CXX)
  set_source_files_properties(${source_file} PROPERTIES LANGUAGE CXX)

  verify_remap_plugin(${name})
endfunction()
