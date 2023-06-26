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

# FindTSMallocReplacement.cmake
#
# This will define the following variables
#
#     TSMallocReplacement_FOUND
#     TS_HAS_MALLOC_REPLACEMENT
#     TS_HAS_JEMALLOC
#     TS_HAS_MIMALLOC
#
# and the following imported targets
#
#     ts::TSMallocReplacement
#

if(ENABLE_JEMALLOC AND ENABLE_MIMALLOC)
    message(FATAL_ERROR "Cannot build with both jemalloc and mimalloc.")
endif()

if(ENABLE_JEMALLOC)
    find_package(jemalloc REQUIRED)
endif()
set(TS_HAS_JEMALLOC ${jemalloc_FOUND})

if(ENABLE_MIMALLOC)
    find_package(mimalloc REQUIRED)
endif()
set(TS_HAS_MIMALLOC ${mimalloc_FOUND})

if(TS_HAS_JEMALLOC OR TS_HAS_MIMALLOC)
    set(TS_HAS_MALLOC_REPLACEMENT TRUE)
endif()

mark_as_advanced(
    TSMallocReplacement_FOUND
    TS_HAS_MALLOC_REPLACEMENT
    TS_HAS_JEMALLOC
    TS_HAS_MIMALLOC
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TSMallocReplacement
    REQUIRED_VARS TS_HAS_MALLOC_REPLACEMENT
)

if(TSMallocReplacement_FOUND AND NOT TARGET ts::TSMallocReplacement)
    add_library(ts::TSMallocReplacement INTERFACE IMPORTED)
    if(TS_HAS_JEMALLOC)
        target_link_libraries(ts::TSMallocReplacement
            INTERFACE
                jemalloc::jemalloc
        )
    elseif(TS_HAS_MIMALLOC)
        add_library(mimalloc::mimalloc ALIAS mimalloc)
        target_link_libraries(ts::TSMallocReplacement
            INTERFACE
                mimalloc::mimalloc
    )
    endif()
endif()
