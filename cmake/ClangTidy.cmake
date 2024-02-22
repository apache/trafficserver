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
#   "CLANG_TIDY_OPTS": "--warnings-as-errors=*"
# }
# ```

if(ENABLE_CLANG_TIDY)
  find_program(
    CLANG_TIDY_EXE
    NAMES "clang-tidy"
    HINTS ${CLANG_TIDY_PATH}
  )
endif()


function(clang_tidy_check target)
  if(ENABLE_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE};${CLANG_TIDY_OPTS};")
  endif()
endfunction()
