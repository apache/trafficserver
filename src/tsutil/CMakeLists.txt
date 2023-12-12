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

# Items that even tscore can depend on
set(TSUTIL_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tsutil/Metrics.h ${PROJECT_SOURCE_DIR}/include/tsutil/SourceLocation.h
    ${PROJECT_SOURCE_DIR}/include/tsutil/DbgCtl.h ${PROJECT_SOURCE_DIR}/include/tsutil/ts_bw_format.h
    ${PROJECT_SOURCE_DIR}/include/tsutil/ts_diag_levels.h ${PROJECT_SOURCE_DIR}/include/tsutil/ts_errata.h
)
add_library(tsutil Assert.cc Metrics.cc DbgCtl.cc SourceLocation.cc ts_diags.cc)
add_library(ts::tsutil ALIAS tsutil)
set_target_properties(tsutil PROPERTIES POSITION_INDEPENDENT_CODE TRUE PUBLIC_HEADER "${TSUTIL_PUBLIC_HEADERS}")
target_link_libraries(tsutil PUBLIC libswoc)

install(TARGETS tsutil PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/api)

if(BUILD_TESTING)
  add_executable(test_tsutil unit_tests/test_Metrics.cc)

  target_link_libraries(test_tsutil PRIVATE tsutil catch2::catch2)

  add_test(NAME test_tsutil COMMAND $<TARGET_FILE:test_tsutil>)
endif()
