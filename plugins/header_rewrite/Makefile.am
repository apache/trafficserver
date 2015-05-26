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

include $(top_srcdir)/build/plugins.mk

pkglib_LTLIBRARIES = header_rewrite.la
header_rewrite_la_SOURCES = \
  condition.cc \
  conditions.cc \
  expander.cc \
  factory.cc \
  header_rewrite.cc \
  lulu.cc \
  operator.cc \
  operators.cc \
  parser.cc \
  regex_helper.cc \
  resources.cc \
  ruleset.cc \
  statement.cc

header_rewrite_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
header_rewrite_la_LIBADD = $(GEO_LIBS)

bin_PROGRAMS = header_rewrite_test
header_rewrite_test_SOURCES = parser.cc header_rewrite_test.cc
header_rewrite_test_CXXFLAGS = $(AM_CXXFLAGS)
