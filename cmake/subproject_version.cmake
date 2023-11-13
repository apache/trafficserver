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

# subproject_version(<subproject-name> <result-variable>)
#
# Extract version of a sub-project, which was previously included with add_subdirectory().
function(subproject_version subproject_name VERSION_VAR)
  # Read CMakeLists.txt for subproject and extract project() call(s) from it.
  file(STRINGS "${${subproject_name}_SOURCE_DIR}/CMakeLists.txt" project_calls REGEX "[ \t]*project\\(")
  # For every project() call try to extract its VERSION option
  foreach(project_call ${project_calls})
    string(REGEX MATCH "VERSION[ ]+([^ )]+)" version_param "${project_call}")
    if(version_param)
      set(version_value "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(version_value)
    set(${VERSION_VAR}
        "${version_value}"
        PARENT_SCOPE
    )
    message("INFO: ${subproject_name} version ${version_value}")
  else()
    message("WARNING: Cannot extract version for subproject '${subproject_name}'")
  endif()
endfunction(subproject_version)
