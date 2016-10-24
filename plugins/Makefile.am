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

# Add new plugins in alphabetic order, but keep experimental always at the bottom.

include $(top_srcdir)/build/plugins.mk
include $(top_srcdir)/build/tidy.mk

check_PROGRAMS =
noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =

SUBDIRS =

AM_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

if BUILD_EXPERIMENTAL_PLUGINS
SUBDIRS += experimental
endif

include authproxy/Makefile.inc
include background_fetch/Makefile.inc
include conf_remap/Makefile.inc
include esi/Makefile.inc
include generator/Makefile.inc
include gzip/Makefile.inc
include header_rewrite/Makefile.inc
include healthchecks/Makefile.inc
include libloader/Makefile.inc
include regex_remap/Makefile.inc
include regex_revalidate/Makefile.inc
include s3_auth/Makefile.inc
include stats_over_http/Makefile.inc
include tcpinfo/Makefile.inc
include xdebug/Makefile.inc

TESTS = $(check_PROGRAMS)

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
