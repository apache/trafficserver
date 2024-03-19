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

# ClangTidy.cmake
#
# This adds a function to enable clang-tidy to the target. The .clang-tidy config file is refered in default.
#
# - e.g.
# ```
# "cacheVariables": {
#   "ENABLE_CLANG_TIDY": true,
#   "CLANG_TIDY_PATH": "/opt/homebrew/opt/llvm/bin/"
#   "CLANG_TIDY_OPTS": "--fix;--warnings-as-errors=*"
# }
# ```

if(ENABLE_CLANG_TIDY)
  # Find clang-tidy program
  find_program(
    CLANG_TIDY_EXE
    NAMES "clang-tidy"
    HINTS ${CLANG_TIDY_PATH}
  )

  # Add options if there
  #
  # CAVEAT: the option should not end with semi-colon. You'll see below error.
  # ```
  # error: unable to handle compilation, expected exactly one compiler job in '' [clang-diagnostic-error]
  # ```
  if(NOT "${CLANG_TIDY_OPTS}" STREQUAL "")
    string(REGEX REPLACE ";$" "$" CLANG_TIDY_OPTS_TRIMMED ${CLANG_TIDY_OPTS})
    string(APPEND CLANG_TIDY_EXE ";${CLANG_TIDY_OPTS_TRIMMED}")
  endif()

  message(STATUS "Enable clang-tidy - ${CLANG_TIDY_EXE}")
endif()

function(clang_tidy_check target)
  if(NOT ENABLE_CLANG_TIDY)
    return()
  endif()

  set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endfunction()
