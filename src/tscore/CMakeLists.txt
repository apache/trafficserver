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



# This executable generates the parse rules that are included in ParseRules.cc
# Add custom commands and dependencies to make sure this gets build and run before compiling libtscore
add_executable(CompileParseRules CompileParseRules.cc)

add_custom_command(
        OUTPUT ParseRulesCType ParseRulesCTypeToUpper ParseRulesCTypeToLower
        COMMAND CompileParseRules
        COMMENT "Generating compile parse rules"
)

add_custom_target(ParseRules ALL DEPENDS ParseRulesCType ParseRulesCTypeToUpper ParseRulesCTypeToLower)

add_library(tscore SHARED
        AcidPtr.cc
        AcidPtr.cc
        Arena.cc
        ArgParser.cc
        BaseLogFile.cc
        BufferWriterFormat.cc
        ConsistentHash.cc
        ContFlags.cc
        CryptoHash.cc
        DbgCtl.cc
        Diags.cc
        Errata.cc
        EventNotify.cc
        Extendible.cc
        HKDF_openssl.cc
        Hash.cc
        HashFNV.cc
        HashSip.cc
        HostLookup.cc
        InkErrno.cc
        JeMiAllocator.cc
        Layout.cc
        LogMessage.cc
        MMH.cc
        MatcherUtils.cc
        ParseRules.cc
        Random.cc
        RbTree.cc
        Regex.cc
        Regression.cc
        SourceLocation.cc
        TextBuffer.cc
        Throttler.cc
        Tokenizer.cc
        Version.cc
        X509HostnameValidator.cc
        hugepages.cc
        ink_args.cc
        ink_assert.cc
        ink_base64.cc
        ink_cap.cc
        ink_defs.cc
        ink_error.cc
        ink_file.cc
        ink_hrtime.cc
        ink_hw.cc
        ink_inet.cc
        ink_memory.cc
        ink_mutex.cc
        ink_queue.cc
        ink_queue_utils.cc
        ink_rand.cc
        ink_res_init.cc
        ink_res_mkquery.cc
        ink_resource.cc
        ink_rwlock.cc
        ink_sock.cc
        ink_sprintf.cc
        ink_stack_trace.cc
        ink_string++.cc
        ink_string.cc
        ink_sys_control.cc
        ink_syslog.cc
        ink_thread.cc
        ink_time.cc
        ink_uuid.cc
        llqueue.cc
        lockfile.cc
        runroot.cc
        signals.cc
        ts_file.cc
)
add_dependencies(tscore ParseRules tscpputil)
target_include_directories(tscore PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${YAML_INCLUDE_DIRS}
)
target_link_libraries(tscore ${PCRE_LIBRARIES} ${OPENSSL_LIBRARIES} yaml-cpp::yaml-cpp tscpputil resolv libswoc)

add_executable(test_tscore
        unit_tests/test_AcidPtr.cc
        unit_tests/test_ArgParser.cc
        unit_tests/test_BufferWriter.cc
        unit_tests/test_BufferWriterFormat.cc
        unit_tests/test_CryptoHash.cc
        unit_tests/test_Errata.cc
        unit_tests/test_Extendible.cc
        unit_tests/test_HKDF.cc
        unit_tests/test_Histogram.cc
        unit_tests/test_History.cc
        unit_tests/test_IntrusiveHashMap.cc
        unit_tests/test_IntrusivePtr.cc
        unit_tests/test_List.cc
        unit_tests/test_MMH.cc
        unit_tests/test_MT_hashtable.cc
        unit_tests/test_ParseRules.cc
        unit_tests/test_PluginUserArgs.cc
        unit_tests/test_PriorityQueue.cc
        unit_tests/test_Ptr.cc
        unit_tests/test_Random.cc
        unit_tests/test_Regex.cc
        unit_tests/test_Throttler.cc
        unit_tests/test_Tokenizer.cc
        unit_tests/test_Version.cc
        unit_tests/test_arena.cc
        unit_tests/test_ink_inet.cc
        unit_tests/test_ink_memory.cc
        unit_tests/test_layout.cc
        unit_tests/test_scoped_resource.cc
        unit_tests/test_ts_file.cc
        unit_tests/unit_test_main.cc
)
target_link_libraries(test_tscore PRIVATE tscore inkevent records_p libswoc)
target_include_directories(test_tscore PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/iocore/eventsystem ${CATCH_INCLUDE_DIR})
