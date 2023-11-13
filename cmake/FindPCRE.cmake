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

# FindPCRE.cmake
#
# This will define the following variables
#
#     PCRE_FOUND
#     PCRE_LIBRARIES
#     PCRE_INCLUDE_DIRS
#
# and the following imported targets
#
#     PCRE::PCRE
#

find_path(PCRE_INCLUDE_DIR NAMES pcre.h) # PATH_SUFFIXES pcre)
find_library(PCRE_LIBRARY NAMES pcre)

mark_as_advanced(PCRE_FOUND PCRE_LIBRARY PCRE_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCRE REQUIRED_VARS PCRE_INCLUDE_DIR PCRE_LIBRARY)

if(PCRE_FOUND)
  set(PCRE_INCLUDE_DIRS "${PCRE_INCLUDE_DIR}")
  set(PCRE_LIBRARIES "${PCRE_LIBRARY}")
endif()

if(PCRE_FOUND AND NOT TARGET PCRE::PCRE)
  add_library(PCRE::PCRE INTERFACE IMPORTED)
  target_include_directories(PCRE::PCRE INTERFACE ${PCRE_INCLUDE_DIRS})
  target_link_libraries(PCRE::PCRE INTERFACE "${PCRE_LIBRARY}")
endif()
