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

# Check128BitCas.cmake
#
# This will define the following variables
#
#     TS_HAS_128BIT_CAS
#     TS_NEEDS_MCX16_FOR_CAS
#

set(CHECK_PROGRAM
    "
    int main(void)
    {
        __int128_t x = 0;
        return __sync_bool_compare_and_swap(&x,0,10);
    }
    "
)

include(CheckCSourceCompiles)
check_c_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_CAS)

if(NOT TS_HAS_128BIT_CAS)
    unset(TS_HAS_128BIT_CAS CACHE)
    set(CMAKE_REQUIRED_FLAGS "-Werror -mcx16")
    check_c_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_CAS)
    set(NEED_MCX16 ${TS_HAS_128BIT_CAS})
    unset(CMAKE_REQUIRED_FLAGS)
endif()

set(TS_NEEDS_MCX16_FOR_CAS
    ${NEED_MCX16}
    CACHE
    BOOL
    "Whether -mcx16 is needed to compile CAS"
)

unset(CHECK_PROGRAM)
unset(NEEDS_MCX16)

mark_as_advanced(TS_HAS_128BIT_CAS TS_NEEDS_MCX16_FOR_CAS)
