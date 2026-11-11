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

find_library(unwind_ptrace_LIBRARY NAMES unwind-ptrace)
find_library(unwind_generic_LIBRARY NAMES unwind-generic)
# LLVM's unwinder also installs a libunwind.so and libunwind.h, which may be
# earlier in the search path but lack the remote unwinding API. Take the ones
# installed alongside libunwind-ptrace. find_library and find_path keep any
# cached result, so drop one that breaks this rule; otherwise an existing build
# directory would never pick up the right copies.
if(unwind_ptrace_LIBRARY)
  get_filename_component(_unwind_library_dir "${unwind_ptrace_LIBRARY}" DIRECTORY)
  if(unwind_LIBRARY)
    get_filename_component(_unwind_cached_dir "${unwind_LIBRARY}" DIRECTORY)
    if(NOT _unwind_cached_dir STREQUAL _unwind_library_dir)
      unset(unwind_LIBRARY CACHE)
    endif()
  endif()
  find_library(unwind_LIBRARY NAMES unwind PATHS "${_unwind_library_dir}" NO_DEFAULT_PATH)
endif()
if(unwind_INCLUDE_DIR
   AND NOT EXISTS "${unwind_INCLUDE_DIR}/libunwind-ptrace.h"
   AND NOT EXISTS "${unwind_INCLUDE_DIR}/libunwind/libunwind-ptrace.h"
)
  unset(unwind_INCLUDE_DIR CACHE)
endif()
find_path(unwind_INCLUDE_DIR NAMES libunwind-ptrace.h libunwind/libunwind-ptrace.h)

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
