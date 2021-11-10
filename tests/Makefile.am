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
include $(top_srcdir)/build/tidy.mk

noinst_LTLIBRARIES =
noinst_PROGRAMS =

SUBDIRS =

AM_LDFLAGS += $(TS_PLUGIN_LD_FLAGS)

# Automake is pretty draconian about not creating shared object (.so) files for
# non-installed files. However we do not want to install our test plugins so
# we prefix them with noinst_.  The following -rpath argument coerces the
# generation of so objects for these test files.
AM_LDFLAGS += -rpath $(abs_builddir)

include gold_tests/bigobj/Makefile.inc
include gold_tests/continuations/plugins/Makefile.inc
include gold_tests/chunked_encoding/Makefile.inc
include gold_tests/pluginTest/tsapi/Makefile.inc
include gold_tests/timeout/Makefile.inc
include gold_tests/tls/Makefile.inc
include tools/plugins/Makefile.inc

TESTS = $(check_PROGRAMS)

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
