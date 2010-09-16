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

SUBDIRS = remote

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmtapilocal.a
libmgmtapilocal_a_SOURCES = \
  include/INKMgmtAPI.h \
  CfgContextDefs.h \
  CfgContextImpl.cc \
  CfgContextImpl.h \
  CfgContextManager.cc \
  CfgContextManager.h \
  CfgContextUtils.cc \
  CfgContextUtils.h \
  CoreAPI.cc \
  CoreAPI.h \
  CoreAPIShared.cc \
  CoreAPIShared.h \
  EventCallback.cc \
  EventCallback.h \
  EventControlMain.cc \
  EventControlMain.h \
  GenericParser.cc \
  GenericParser.h \
  INKMgmtAPI.cc \
  MgmtPlugin.cc \
  MgmtPlugin.h \
  NetworkUtilsDefs.h \
  NetworkUtilsLocal.cc \
  NetworkUtilsLocal.h \
  TSControlMain.cc \
  TSControlMain.h
