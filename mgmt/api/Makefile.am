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

SUBDIRS = remote include

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/cluster \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/mgmt/web2 \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/lib

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmtapilocal.a
libmgmtapilocal_a_SOURCES = \
  include/mgmtapi.h \
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
  NetworkUtilsDefs.h \
  NetworkUtilsLocal.cc \
  NetworkUtilsLocal.h \
  TSControlMain.cc \
  TSControlMain.h
