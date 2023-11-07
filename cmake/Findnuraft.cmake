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

# Findnuraft.cmake
#
# This will define the following variables
#
#     nuraft_FOUND
#
# and the following imported targets
#
#     nuraft::nuraft
#

find_library(nuraft_LIBRARY nuraft)
find_path(
  nuraft_INCLUDE_DIR
  NAMES libnuraft/nuraft.hxx
)

mark_as_advanced(nuraft_FOUND nuraft_LIBRARY nuraft_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(nuraft REQUIRED_VARS nuraft_LIBRARY nuraft_INCLUDE_DIR)

# Add the library but only add libraries if nuraft is found
add_library(nuraft::nuraft INTERFACE IMPORTED)
if(nuraft_FOUND)
  set(nuraft_INCLUDE_DIRS ${nuraft_INCLUDE_DIR})
  target_include_directories(nuraft::nuraft INTERFACE ${nuraft_INCLUDE_DIRS})
  target_link_libraries(nuraft::nuraft INTERFACE ${nuraft_LIBRARY})
endif()
