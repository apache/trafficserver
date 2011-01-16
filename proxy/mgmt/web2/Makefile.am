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

AM_CPPFLAGS = \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy/mgmt \
  -I$(top_srcdir)/proxy/mgmt/api \
  -I$(top_srcdir)/proxy/mgmt/api/include \
  -I$(top_srcdir)/proxy/mgmt/cli \
  -I$(top_srcdir)/proxy/mgmt/cluster \
  -I$(top_srcdir)/proxy/mgmt/utils \
  -I$(top_srcdir)/lib/tsconfig \
  -I$(top_srcdir)/proxy/mgmt/tools \
  -I$(top_srcdir)/proxy/hdrs

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

noinst_LIBRARIES = libweb.a
libweb_a_SOURCES = \
  MgmtAllow.cc \
  MgmtAllow.h \
  MgmtRaf.h \
  WebCompatibility.cc \
  WebCompatibility.h \
  WebConfig.cc \
  WebConfig.h \
  WebConfigRender.cc \
  WebConfigRender.h \
  WebGlobals.h \
  WebHttpMessage.cc \
  WebHttpMessage.h \
  WebHttpRender.cc \
  WebHttpRender.h \
  WebOverview.cc \
  WebOverview.h \
  WebUtils.cc \
  WebUtils.h

if BUILD_WEBUI
libweb_a_SOURCES += \
  WebHttpTree.cc \
  WebHttpTree.h
endif
