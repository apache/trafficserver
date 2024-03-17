#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

add_executable(traffic_layout engine.cc file_system.cc info.cc traffic_layout.cc)

target_link_libraries(traffic_layout PRIVATE ts::inkevent ts::records yaml-cpp::yaml-cpp ts::tscore ts::tsutil)

if(TS_USE_HWLOC)
  target_link_libraries(traffic_layout PRIVATE hwloc::hwloc)
endif()

if(HAVE_LZMA_H)
  target_link_libraries(traffic_layout PRIVATE LibLZMA::LibLZMA)
endif()

if(HAVE_BROTLI_ENCODE_H)
  target_link_libraries(traffic_layout PRIVATE brotli::brotlienc)
endif()

install(TARGETS traffic_layout)

clang_tidy_check(traffic_layout)
