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

find_library(resolv_LIBRARY resolv)
find_path(resolv_INCLUDE_DIR resolv.h)

mark_as_advanced(resolv_FOUND resolv_LIBRARY resolv_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(resolv
    REQUIRED_VARS resolv_LIBRARY resolv_INCLUDE_DIR
)

# Add the library but only add libraries if resolv is found
add_library(resolv::resolv INTERFACE IMPORTED)
if(resolv_FOUND AND NOT TARGET resolv::resolv)
  target_include_directories(resolv::resolv INTERFACE ${resolv_INCLUDE_DIRS})
  target_link_libraries(resolv::resolv INTERFACE ${resolv_LIBRARY})
endif()
