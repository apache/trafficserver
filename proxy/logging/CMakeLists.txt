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


add_library(logging STATIC
        Log.cc
        LogAccess.cc
        LogBuffer.cc
        LogConfig.cc
        LogField.cc
        LogFieldAliasMap.cc
        LogFile.cc
        LogFilter.cc
        LogFormat.cc
        LogObject.cc
        LogUtils.cc
        RolledLogDeleter.cc
        YamlLogConfig.cc
        YamlLogConfigDecoders.cc
)
add_library(ts::logging ALIAS logging)

target_include_directories(logging PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${SWOC_INCLUDE_DIR}
)

target_link_libraries(logging
    PUBLIC
        ts::inkevent
        ts::inkutils
        ts::proxy
        ts::tscore
        yaml-cpp::yaml-cpp
)

if(BUILD_TESTING)
  add_executable(test_LogUtils
    LogUtils.cc
    unit-tests/test_LogUtils.cc
  )
  target_compile_definitions(test_LogUtils PRIVATE TEST_LOG_UTILS)
  target_include_directories(test_LogUtils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(test_LogUtils tscore records catch2::catch2)
  add_test(NAME test_LogUtils COMMAND test_LogUtils)

  add_executable(test_RolledLogDeleter
    LogUtils.cc
    RolledLogDeleter.cc
    unit-tests/test_LogUtils.cc
  )
  target_compile_definitions(test_RolledLogDeleter PRIVATE TEST_LOG_UTILS)
  target_include_directories(test_RolledLogDeleter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(test_RolledLogDeleter tscore records catch2::catch2)
  add_test(NAME test_RolledLogDeleter COMMAND test_RolledLogDeleter)
endif()
