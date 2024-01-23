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

# Findunwind.cmake
#
# This will define the following variables
#
#     unwind_FOUND
#     unwind_LIBRARY
#     unwind_INCLUDE_DIRS
#
# and the following imported targets
#
#     unwind::unwind
#

find_library(unwind_LIBRARY NAMES unwind)
find_library(unwind_ptrace_LIBRARY NAMES unwind-ptrace)
find_library(unwind_generic_LIBRARY NAMES unwind-generic)
find_path(unwind_INCLUDE_DIR NAMES libunwind.h libunwind/libunwind.h)

mark_as_advanced(unwind_FOUND unwind_LIBRARY unwind_ptrace_LIBRARY unwind_generic_LIBRARY unwind_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  unwind REQUIRED_VARS unwind_LIBRARY unwind_ptrace_LIBRARY unwind_generic_LIBRARY unwind_INCLUDE_DIR
)

if(unwind_FOUND)
  set(unwind_INCLUDE_DIRS ${unwind_INCLUDE_DIR})
endif()

if(unwind_FOUND AND NOT TARGET unwind::unwind)
  add_library(unwind::unwind INTERFACE IMPORTED)
  target_include_directories(unwind::unwind INTERFACE ${unwind_INCLUDE_DIRS})
  target_link_libraries(
    unwind::unwind INTERFACE "${unwind_ptrace_LIBRARY}" "${unwind_generic_LIBRARY}" "${unwind_LIBRARY}"
  )
endif()
