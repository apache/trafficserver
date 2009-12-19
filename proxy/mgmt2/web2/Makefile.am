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
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/tools \
  -I$(top_srcdir)/proxy \
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
  WebHttpAuth.cc \
  WebHttpAuth.h \
  WebHttp.cc \
  WebHttp.h \
  WebHttpContext.cc \
  WebHttpContext.h \
  WebHttpLog.cc \
  WebHttpLog.h \
  WebHttpMessage.cc \
  WebHttpMessage.h \
  WebHttpRender.cc \
  WebHttpRender.h \
  WebHttpSession.cc \
  WebHttpSession.h \
  WebHttpTree.cc \
  WebHttpTree.h \
  WebIntrMain.cc \
  WebIntrMain.h \
  WebOverview.cc \
  WebOverview.h \
  WebReconfig.cc \
  WebReconfig.h \
  WebUtils.cc \
  WebUtils.h
