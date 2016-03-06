# Makefile.am for the lib/records
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
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/lib

noinst_LIBRARIES = librecords_lm.a librecords_p.a librecords_cop.a

librecords_COMMON = \
  I_RecAlarms.h \
  I_RecCore.h \
  I_RecDefs.h \
  I_RecEvents.h \
  I_RecHttp.h \
  I_RecMutex.h \
  I_RecSignals.h \
  P_RecCore.cc \
  P_RecCore.h \
  P_RecDefs.h \
  P_RecFile.h \
  P_RecFile.h \
  P_RecMessage.h \
  P_RecUtils.h \
  RecConfigParse.cc \
  RecCore.cc \
  RecDebug.cc \
  RecFile.cc \
  RecHttp.cc \
  RecMessage.cc \
  RecMutex.cc \
  RecRawStats.cc \
  RecUtils.cc

librecords_lm_a_SOURCES = \
  $(librecords_COMMON) \
  I_RecLocal.h \
  P_RecLocal.h \
  RecLocal.cc

librecords_p_a_SOURCES = \
  $(librecords_COMMON) \
  I_RecProcess.h \
  P_RecProcess.h \
  RecProcess.cc

librecords_cop_a_SOURCES = \
  RecConfigParse.cc \
  RecFile.cc \
  RecDebug.cc
