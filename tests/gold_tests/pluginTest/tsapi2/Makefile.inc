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

# This file is included in test/Makefile.am under repo root directory.

noinst_LTLIBRARIES += gold_tests/pluginTest/tsapi2/test_tsapi2.la
gold_tests_pluginTest_tsapi2_test_tsapi2_la_SOURCES = \
  gold_tests/pluginTest/tsapi2/test_tsapi2.cc \
  gold_tests/pluginTest/tsapi2/alt_info.h \
  gold_tests/pluginTest/tsapi2/cache.h \
  gold_tests/pluginTest/tsapi2/hooks.h \
  gold_tests/pluginTest/tsapi2/parent_proxy.h \
  gold_tests/pluginTest/tsapi2/ssn.h \
  gold_tests/pluginTest/tsapi2/transform.h
