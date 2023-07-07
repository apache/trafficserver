#
# Makefile.am for the Enterprise Management module.
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

SUBDIRS = config rpc

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	@SWOC_INCLUDES@ \
	@YAMLCPP_INCLUDES@ \
	$(TS_INCLUDES)

include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
