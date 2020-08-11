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

pkglib_LTLIBRARIES += experimental/access_control/access_control.la

experimental_access_control_access_control_la_SOURCES = \
  experimental/access_control/access_control.cc \
  experimental/access_control/common.cc \
  experimental/access_control/config.cc \
  experimental/access_control/headers.cc \
  experimental/access_control/pattern.cc \
  experimental/access_control/plugin.cc \
  experimental/access_control/utils.cc

check_PROGRAMS +=  experimental/access_control/test_access_control

experimental_access_control_test_access_control_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DACCESS_CONTROL_UNIT_TEST
experimental_access_control_test_access_control_LDADD = $(OPENSSL_LIBS) $(top_builddir)/src/tscore/libtscore.la

experimental_access_control_test_access_control_SOURCES = \
    experimental/access_control/unit_tests/test_access_control.cc \
    experimental/access_control/unit_tests/test_utils.cc \
    experimental/access_control/access_control.cc \
    experimental/access_control/common.cc \
    experimental/access_control/utils.cc
