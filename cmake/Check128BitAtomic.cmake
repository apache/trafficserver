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

# Check128BitAtomic.cmake
#
# This will define the following variables
#
#     TS_HAS_128BIT_ATOMIC
#     TS_NEEDS_MCX16_FOR_128BIT_ATOMIC
#

set(CHECK_PROGRAM
    "
    #include <atomic>

    int main()
    {
        std::atomic<__int128> x{0};
        __int128 expected{x.load()};
        return x.compare_exchange_strong(expected, 10);
    }
    "
)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_ATOMIC)

set(NEED_MCX16 FALSE)

if(NOT TS_HAS_128BIT_ATOMIC)
  unset(TS_HAS_128BIT_ATOMIC CACHE)
  set(CMAKE_REQUIRED_FLAGS "-Werror -mcx16")
  check_cxx_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_ATOMIC)
  set(NEED_MCX16 ${TS_HAS_128BIT_ATOMIC})
  unset(CMAKE_REQUIRED_FLAGS)
endif()

set(TS_NEEDS_MCX16_FOR_128BIT_ATOMIC
    ${NEED_MCX16}
    CACHE BOOL "Whether -mcx16 is needed to compile 128bit atomics"
)

unset(CHECK_PROGRAM)
unset(NEED_MCX16)

mark_as_advanced(TS_HAS_128BIT_ATOMIC TS_NEEDS_MCX16_FOR_128BIT_ATOMIC)
