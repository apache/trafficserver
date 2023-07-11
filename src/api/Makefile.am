# libts Makefile.am
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

lib_LTLIBRARIES = libtsapi.la

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/include \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

libtsapi_la_LDFLAGS = @AM_LDFLAGS@ -no-undefined -version-info @TS_LIBTOOL_VERSION@ @SWOC_LDFLAGS@

libtsapi_la_LIBADD = \
    $(top_builddir)/src/tscore/libtscore.la \
	@SWOC_LIBS@


libtsapi_la_SOURCES = \
	Metrics.cc

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
