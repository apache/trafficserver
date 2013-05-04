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
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/api \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/mgmt/cluster \
  -I$(top_srcdir)/mgmt/web2 \
  -I$(top_srcdir)/lib/tsconfig \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/lib

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

# header files used by other libraries
EXTRA_DIST = MgmtHashTable.h MgmtSocket.h

noinst_LIBRARIES = libutils_lm.a libutils_p.a

libutils_lm_a_CPPFLAGS = $(ink_with_modules_local) $(AM_CPPFLAGS)
libutils_lm_a_SOURCES = \
  EnvBlock.cc \
  EnvBlock.h \
  ExpandingArray.cc \
  ExpandingArray.h \
  MgmtSchema.cc \
  MgmtSchema.h \
  MgmtUtils.cc \
  MgmtUtils.h \
  WebMgmtUtils.cc \
  WebMgmtUtils.h \
  XmlUtils.cc \
  XmlUtils.h

libutils_p_a_SOURCES = \
  MgmtUtils.cc \
  MgmtUtils.h \
  XmlUtils.cc \
  XmlUtils.h
