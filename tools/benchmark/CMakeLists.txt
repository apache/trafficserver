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

add_executable(benchmark_FreeList benchmark_FreeList.cc)
target_link_libraries(benchmark_FreeList PRIVATE catch2::catch2 ts::tscore libswoc::libswoc)
if(TS_USE_HWLOC)
  target_link_libraries(benchmark_FreeList PRIVATE hwloc)
endif()

add_executable(benchmark_ProxyAllocator benchmark_ProxyAllocator.cc)
target_link_libraries(benchmark_ProxyAllocator PRIVATE catch2::catch2 ts::tscore ts::inkevent libswoc::libswoc)

add_executable(benchmark_SharedMutex benchmark_SharedMutex.cc)
target_link_libraries(benchmark_SharedMutex PRIVATE catch2::catch2 ts::tscore libswoc::libswoc)
