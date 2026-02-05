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

# Findcap.cmake
#
# This will define the following variables
#
#     cap_FOUND
#     cap_LIBRARY
#     cap_INCLUDE_DIRS
#
# and the following imported targets
#
#     cap::cap
#

# Check for Linux libcap
find_library(cap_LIBRARY NAMES cap)
find_path(cap_INCLUDE_DIR NAMES sys/capability.h)

# Check for FreeBSD Capsicum (Base system)
find_path(capsicum_INCLUDE_DIR NAMES sys/capsicum.h)

if(capsicum_INCLUDE_DIR)
  # FreeBSD native path
  add_library(cap::cap INTERFACE IMPORTED)
  target_compile_definitions(cap::cap INTERFACE HAVE_SYS_CAPSICUM_H)
  # No target_link_libraries needed as it's in libc
elseif(cap_LIBRARY AND cap_INCLUDE_DIR)
  # Linux/Other path
  add_library(cap::cap INTERFACE IMPORTED)
  target_include_directories(cap::cap INTERFACE ${cap_INCLUDE_DIR})
  target_link_libraries(cap::cap INTERFACE ${cap_LIBRARY})
  target_compile_definitions(cap::cap INTERFACE HAVE_SYS_CAPABILITY_H)
endif()
