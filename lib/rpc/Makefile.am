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

SUBDIRS = utils

AM_CPPFLAGS += \
  $(iocore_include_dirs) \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/api \
  -I$(abs_top_srcdir)/mgmt/utils \
  -I$(abs_top_srcdir)/mgmt/api/include \
  -I$(abs_top_srcdir)/lib/tsconfig \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/lib \
  $(TS_INCLUDES) \
  @OPENSSL_INCLUDES@

# two libraries because ServerControl needs the EventSystem which
# pulls alot of dependencies that the ClientControl doesn't need
noinst_LTLIBRARIES = librpc_c.la librpc_s.la

librpc_c_la_SOURCES = \
  ClientControl.cc \
  ClientControl.h 

librpc_s_la_SOURCES = \
  ServerControl.cc \
  ServerControl.h 

librpc_c_la_LIBADD = \
  $(top_builddir)/lib/rpc/utils/librpc_utils.la

librpc_s_la_LIBADD = \
  $(top_builddir)/lib/rpc/utils/librpc_utils.la

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
