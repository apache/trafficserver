# proxy Makefile.am
#
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

include $(top_srcdir)/build/tidy.mk

# Note that hdrs is targeted from ../Makefile.am
noinst_LIBRARIES = \
	libdiagsconfig.a \
	libUglyLogStubs.a

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include/records \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/api \
	-I$(abs_top_srcdir)/mgmt/api/include \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/include \
	$(TS_INCLUDES)

libdiagsconfig_a_SOURCES = \
	DiagsConfig.cc

libUglyLogStubs_a_SOURCES = \
	UglyLogStubs.cc

clang-tidy-local: $(libdiagsconfig_a_SOURCES) $(libUglyLogStubs_a_SOURCES)
	$(CXX_Clang_Tidy)
