# Makefile.am for the traffic/iocore/utils hierarchy
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
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/libinktomi++ \
  -DPREFIX=\"$(prefix)\" \
  -DEXEC_PREFIX=\"$(rel_exec_prefix)\" \
  -DBINDIR=\"$(rel_bindir)\" \
  -DSBINDIR=\"$(rel_sbindir)\" \
  -DSYSCONFDIR=\"$(rel_sysconfdir)\" \
  -DDATADIR=\"$(rel_datadir)\" \
  -DINCLUDEDIR=\"$(rel_includedir)\" \
  -DLIBDIR=\"$(rel_libdir)\" \
  -DLIBEXECDIR=\"$(rel_libexecdir)\" \
  -DLOCALSTATEDIR=\"$(rel_localstatedir)\" \
  -DSHAREDSTATEDIR=\"$(rel_sharedstatedir)\" \
  -DMANDIR=\"$(rel_mandir)\" \
  -DCACHEDIR=\"$(rel_cachedir)\" \
  -DINFODIR=\"$(rel_infodir)\"

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkutils.a

libinkutils_a_SOURCES = \
  I_Layout.h \
  I_Machine.h \
  I_MTInteractor.h \
  I_OneWayMultiTunnel.h \
  I_OneWayTunnel.h \
  I_OpQueue.h \
  I_RadixSort.h \
  Layout.cc \
  MTInteractor.cc \
  Machine.cc \
  OneWayMultiTunnel.cc \
  OneWayTunnel.cc \
  OpQueue.cc \
  RadixSort.cc

