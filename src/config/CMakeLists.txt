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

set(CONFIG_PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/include/config/ssl_multicert.h)

add_library(tsconfig ssl_multicert.cc)

add_library(ts::config ALIAS tsconfig)

set_target_properties(tsconfig PROPERTIES POSITION_INDEPENDENT_CODE TRUE PUBLIC_HEADER "${CONFIG_PUBLIC_HEADERS}")

target_include_directories(tsconfig PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(tsconfig PUBLIC libswoc::libswoc yaml-cpp::yaml-cpp)

if(BUILD_SHARED_LIBS)
  install(
    TARGETS tsconfig
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/config
  )
else()
  install(FILES ${CONFIG_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/config)
endif()

clang_tidy_check(tsconfig)

if(BUILD_TESTING)
  add_executable(test_tsconfig unit_tests/test_ssl_multicert.cc)

  target_link_libraries(test_tsconfig PRIVATE tsconfig Catch2::Catch2WithMain)

  add_catch2_test(NAME test_tsconfig COMMAND $<TARGET_FILE:test_tsconfig>)
endif()
