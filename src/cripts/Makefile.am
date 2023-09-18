#
# Makefile.am for Cripts module.
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

include $(top_srcdir)/mk/tidy.mk

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/iocore/eventsystem \
	-I$(abs_top_srcdir)/iocore/utils \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/ \
	@SWOC_INCLUDES@

noinst_LIBRARIES = libcripts.a

libcripts_a_SOURCES = \
	Configs.cc \
	Connections.cc \
	Crypto.cc \
	Error.cc \
	Files.cc \
	Geo.cc \
	Headers.cc \
	Instance.cc \
	Lulu.cc \
	Matcher.cc \
	Metrics.cc \
	Plugins.cc \
	UUID.cc \
	Urls.cc \
	Bundles/Common.cc \
	Bundles/LogsMetrics.cc

clang-tidy-local: $(sort $(DIST_SOURCES))
	$(CXX_Clang_Tidy)
