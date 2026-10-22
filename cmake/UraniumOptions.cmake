# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements. See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Release branches must accept cache entries and commands from older releases.
# Migrate once without allowing old OFF defaults to disable Uranium requests.
foreach(suffix IN ITEMS "" "_UDS")
  if(DEFINED ENABLE_AUTEST${suffix})
    set(deprecated_value "${ENABLE_AUTEST${suffix}}")
    message(DEPRECATION "ENABLE_AUTEST${suffix} is deprecated; use ENABLE_URTEST${suffix}")
    if(NOT DEFINED ENABLE_URTEST${suffix} OR deprecated_value)
      set(ENABLE_URTEST${suffix}
          "${deprecated_value}"
          CACHE BOOL "Set up Uranium tests${suffix}" FORCE
      )
    endif()
    unset(ENABLE_AUTEST${suffix} CACHE)
    unset(ENABLE_AUTEST${suffix})
  endif()
endforeach()
foreach(suffix IN ITEMS SANDBOX OPTIONS)
  if(DEFINED AUTEST_${suffix})
    message(DEPRECATION "AUTEST_${suffix} is deprecated; use URTEST_${suffix}")
    if(NOT DEFINED URTEST_${suffix})
      if(suffix STREQUAL "OPTIONS" AND NOT "${AUTEST_OPTIONS}" STREQUAL "")
        message(STATUS "Migrating AUTEST_OPTIONS unchanged; use pytest arguments (for example -k, not AuTest's -f)")
      endif()
      set(URTEST_${suffix}
          "${AUTEST_${suffix}}"
          CACHE STRING "Uranium test ${suffix}"
      )
    endif()
    unset(AUTEST_${suffix} CACHE)
    unset(AUTEST_${suffix})
  endif()
endforeach()
option(ENABLE_URTEST "Set up Uranium tests (default OFF)")
option(ENABLE_URTEST_UDS "Set up Uranium tests with curl using UDS (default OFF)")
