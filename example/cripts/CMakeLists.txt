# ##############################################################################
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

file(GLOB CRIPTS_EXAMPLES ${PROJECT_SOURCE_DIR}/example/cripts/*.cc)
foreach(EXA ${CRIPTS_EXAMPLES})
  get_filename_component(EXA ${EXA} NAME_WLE)
  add_atsplugin(cripts_${EXA} ${EXA}.cc)
  target_link_libraries(cripts_${EXA} PRIVATE ts::cripts)
endforeach()

# This is a hack, but useful to run clang-tidy across all of Cripts
# Todo: why is this not working??
#find_package(fmt 8.1 REQUIRED)
#$<TARGET_PROPERTY:fmt::fmt,INCLUDE_DIRECTORIES>

set(cript-includes -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/lib/swoc/include -I${CMAKE_BINARY_DIR}/include
                   -I${OPENSSL_INCLUDE_DIR} -I/opt/homebrew/include -I/usr/local/include
)

add_custom_target(
  cripts-clang-tidy COMMAND clang-tidy --config-file ${PROJECT_SOURCE_DIR}/tools/cripts/clang-tidy.conf --fix
                            ${PROJECT_SOURCE_DIR}/example/cripts/example1.cc -- -std=c++17 ${cript-includes}
)
