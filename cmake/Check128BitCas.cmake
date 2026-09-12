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
#     TS_HAS_128BIT_CAS_LIBATOMIC
#     TS_NEEDS_LIBATOMIC_FOR_CAS
#
# TS_HAS_128BIT_CAS means the 16-byte __sync builtins compile and link, which the
# compiler only allows when it can emit an inline lock-free sequence.
#
# TS_HAS_128BIT_CAS_LIBATOMIC is the fallback for targets with no inline 128-bit
# CAS (e.g. riscv64): the __atomic builtins lower to libatomic calls, which may
# be lock-based there.  The __sync builtins never lower to libatomic calls, so
# the fallback has to use __atomic.  The two are mutually exclusive.
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

set(CHECK_PROGRAM_ATOMIC
    "
    int main(void)
    {
        __int128_t x = 0;
        __int128_t y = 0;
        __atomic_load(&x, &y, __ATOMIC_SEQ_CST);
        return !__atomic_compare_exchange_n(&x, &y, 10, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    "
)

set(NEED_MCX16 FALSE)
set(USE_LIBATOMIC_CAS FALSE)
set(NEED_LIBATOMIC FALSE)

include(CheckCSourceCompiles)
check_c_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_CAS)

if(NOT TS_HAS_128BIT_CAS)
  unset(TS_HAS_128BIT_CAS CACHE)
  set(CMAKE_REQUIRED_FLAGS "-Werror -mcx16")
  check_c_source_compiles("${CHECK_PROGRAM}" TS_HAS_128BIT_CAS)
  set(NEED_MCX16 ${TS_HAS_128BIT_CAS})
  unset(CMAKE_REQUIRED_FLAGS)
endif()

if(NOT TS_HAS_128BIT_CAS)
  check_c_source_compiles("${CHECK_PROGRAM_ATOMIC}" TS_HAS_128BIT_CAS_BUILTIN_ATOMIC)
  if(TS_HAS_128BIT_CAS_BUILTIN_ATOMIC)
    set(USE_LIBATOMIC_CAS TRUE)
  else()
    unset(TS_HAS_128BIT_CAS_BUILTIN_ATOMIC CACHE)
    set(CMAKE_REQUIRED_LIBRARIES atomic)
    check_c_source_compiles("${CHECK_PROGRAM_ATOMIC}" TS_HAS_128BIT_CAS_BUILTIN_ATOMIC)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(TS_HAS_128BIT_CAS_BUILTIN_ATOMIC)
      set(USE_LIBATOMIC_CAS TRUE)
      set(NEED_LIBATOMIC TRUE)
    endif()
  endif()
endif()

set(TS_NEEDS_MCX16_FOR_CAS
    ${NEED_MCX16}
    CACHE BOOL "Whether -mcx16 is needed to compile CAS"
)

set(TS_HAS_128BIT_CAS_LIBATOMIC
    ${USE_LIBATOMIC_CAS}
    CACHE BOOL "Whether 128-bit CAS uses the __atomic builtins as a fallback"
)

set(TS_NEEDS_LIBATOMIC_FOR_CAS
    ${NEED_LIBATOMIC}
    CACHE BOOL "Whether libatomic is needed to link CAS"
)

unset(CHECK_PROGRAM)
unset(CHECK_PROGRAM_ATOMIC)
unset(NEED_MCX16)
unset(USE_LIBATOMIC_CAS)
unset(NEED_LIBATOMIC)

mark_as_advanced(TS_HAS_128BIT_CAS TS_NEEDS_MCX16_FOR_CAS TS_HAS_128BIT_CAS_LIBATOMIC TS_NEEDS_LIBATOMIC_FOR_CAS)
