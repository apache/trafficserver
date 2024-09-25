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

add_library(
  inkcache STATIC
  AggregateWriteBuffer.cc
  Cache.cc
  CacheDir.cc
  CacheDisk.cc
  CacheDoc.cc
  CacheEvacuateDocVC.cc
  CacheHosting.cc
  CacheHttp.cc
  CacheProcessor.cc
  CacheRead.cc
  CacheVC.cc
  CacheWrite.cc
  HttpTransactCache.cc
  PreservationTable.cc
  RamCacheCLFUS.cc
  RamCacheLRU.cc
  Store.cc
  Stripe.cc
  StripeSM.cc
)
add_library(ts::inkcache ALIAS inkcache)

if(BUILD_REGRESSION_TESTING)
  target_sources(inkcache PRIVATE CacheTest.cc)
  target_link_libraries(inkcache PRIVATE ts::proxy)
endif()

target_include_directories(inkcache PRIVATE ${CMAKE_SOURCE_DIR}/lib)

target_link_libraries(
  inkcache
  PUBLIC ts::aio ts::hdrs ts::inkevent ts::tscore ts::tsapibackend
  PRIVATE fastlz ZLIB::ZLIB
)

if(HAVE_LZMA_H)
  target_link_libraries(inkcache PRIVATE LibLZMA::LibLZMA)
endif()

if(BUILD_TESTING)
  macro(add_cache_test name)
    add_executable(${name} unit_tests/main.cc unit_tests/stub.cc unit_tests/CacheTestHandler.cc ${ARGN})
    target_link_libraries(${name} PRIVATE ts::inkcache catch2::catch2)
    add_test(NAME test_cache_${name} COMMAND $<TARGET_FILE:${name}>)
  endmacro()

  add_cache_test(Cache unit_tests/test_Cache.cc)
  add_cache_test(Populated_Cache unit_tests/test_Populated_Cache.cc)
  if(ENABLE_DISK_FAILURE_TESTS)
    foreach(i RANGE 1)
      add_cache_test(Disk_Init_Failure_${i} unit_tests/test_Disk_Init_Failure.cc)
      target_compile_definitions(Disk_Init_Failure_${i} PUBLIC FAILURE_INDICES={${i}})
    endforeach()
    foreach(i RANGE 5 20)
      add_cache_test(Disk_Failure_${i} unit_tests/test_Disk_Failure.cc)
      target_compile_definitions(Disk_Failure_${i} PUBLIC FAILURE_INDICES={${i}})
    endforeach()
    add_cache_test(Populated_Cache_Disk_Failure unit_tests/test_Populated_Cache_Disk_Failure.cc)
  endif()
  add_cache_test(CacheDir unit_tests/test_CacheDir.cc)
  add_cache_test(CacheVol unit_tests/test_CacheVol.cc)
  add_cache_test(RWW unit_tests/test_RWW.cc)
  add_cache_test(Alternate_L_to_S unit_tests/test_Alternate_L_to_S.cc)
  add_cache_test(Alternate_S_to_L unit_tests/test_Alternate_S_to_L.cc)
  add_cache_test(Alternate_L_to_S_remove_L unit_tests/test_Alternate_L_to_S_remove_L.cc)
  add_cache_test(Alternate_L_to_S_remove_S unit_tests/test_Alternate_L_to_S_remove_S.cc)
  add_cache_test(Alternate_S_to_L_remove_L unit_tests/test_Alternate_S_to_L_remove_L.cc)
  add_cache_test(Alternate_S_to_L_remove_S unit_tests/test_Alternate_S_to_L_remove_S.cc)
  add_cache_test(Update_L_to_S unit_tests/test_Update_L_to_S.cc)
  add_cache_test(Update_S_to_L unit_tests/test_Update_S_to_L.cc)
  add_cache_test(Update_Header unit_tests/test_Update_header.cc)
  add_cache_test(CacheStripe unit_tests/test_Stripe.cc)
  add_cache_test(CacheAggregateWriteBuffer unit_tests/test_AggregateWriteBuffer.cc)

endif()

clang_tidy_check(inkcache)
