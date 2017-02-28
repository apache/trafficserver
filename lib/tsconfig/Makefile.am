# Makefile.am for TS Config module.
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

lib_LTLIBRARIES = libtsconfig.la
check_PROGRAMS = test-tsconfig
libtsconfig_la_LDFLAGS = -no-undefined -version-info @TS_LIBTOOL_VERSION@

AM_CFLAGS += @FLEX_CFLAGS@
AM_YFLAGS = --yacc -d -p tsconfig

AM_CPPFLAGS = \
  -I$(abs_top_srcdir)/lib

libtsconfig_la_SOURCES = \
  Errata.cc \
  Errata.h \
  IntrusivePtr.h \
  NumericType.h \
  TsBuilder.cc \
  TsBuilder.h \
  TsConfigLexer.h \
  TsConfigParseEvents.h \
  TsConfigTypes.h \
  TsErrataUtil.cc \
  TsErrataUtil.h \
  TsValue.cc \
  TsValue.h

# Depend on the original grammar files only if we can build them.
if BUILD_TSCONFIG_GRAMMAR
  libtsconfig_la_SOURCES += TsConfigGrammar.y TsConfigSyntax.l
else
  libtsconfig_la_SOURCES += TsConfigGrammar.c TsConfigGrammar.h TsConfigSyntax.c

# Prevent automatic rules from firing if invalid.
TsConfigSyntax.c: ;
TsConfigGrammar.h: ;
TsConfigGrammar.c: ;

endif

test_tsconfig_SOURCES = test-tsconfig.cc

test_tsconfig_LDADD = libtsconfig.la ../ts/libtsutil.la

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
