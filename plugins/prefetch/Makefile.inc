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

pkglib_LTLIBRARIES += prefetch/prefetch.la
prefetch_prefetch_la_SOURCES = \
  prefetch/plugin.cc \
  prefetch/common.cc \
  prefetch/configs.cc \
  prefetch/fetch.cc \
  prefetch/headers.cc \
  prefetch/pattern.cc \
  prefetch/fetch_policy.cc \
  prefetch/fetch_policy_simple.cc \
  prefetch/fetch_policy_lru.cc \
  prefetch/evaluate.cc

check_PROGRAMS += prefetch/test_evaluate

prefetch_test_evaluate_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DPREFETCH_UNIT_TEST
prefetch_test_evaluate_SOURCES = prefetch/test/test_evaluate.cc prefetch/evaluate.cc prefetch/common.cc
