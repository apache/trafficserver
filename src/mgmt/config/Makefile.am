#
# Makefile.am for the config Management module.
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

AM_CPPFLAGS += \
    $(iocore_include_dirs) \
    -I$(abs_top_srcdir)/iocore/utils \
    -I$(abs_top_srcdir)/include \
    -I$(abs_top_srcdir)/lib/ \
    -I$(abs_top_srcdir)/include/mgmt \
    -I$(abs_top_srcdir)/proxy/ \
    -I$(abs_top_srcdir)/proxy/http \
    -I$(abs_top_srcdir)/proxy/hdrs \
    $(TS_INCLUDES) @SWOC_INCLUDES@ \
    @YAMLCPP_INCLUDES@

# ^^ all the proxy is to include the PluginCallbacks.

noinst_LIBRARIES = libconfigmanager.a

TESTS = $(check_PROGRAMS)

# Protocol library only, no transport.
libconfigmanager_a_SOURCES = \
	FileManager.cc \
	AddConfigFilesHere.cc

include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
