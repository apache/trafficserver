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

bin_PROGRAMS =
check_PROGRAMS =
TESTS =

include traffic_cache_tool/Makefile.inc
include traffic_via/Makefile.inc
include traffic_top/Makefile.inc
include traffic_manager/Makefile.inc
include traffic_server/Makefile.inc
include traffic_logstats/Makefile.inc
include traffic_crashlog/Makefile.inc
include traffic_ctl/Makefile.inc
include traffic_layout/Makefile.inc
include traffic_logcat/Makefile.inc

if BUILD_WCCP
include traffic_wccp/Makefile.inc
endif

TESTS += \
	$(check_PROGRAMS)

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
