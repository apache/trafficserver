#
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

AM_CPPFLAGS = \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -DPKGSYSUSER=\"$(pkgsysuser)\"

noinst_LIBRARIES = libtsconfig.a
#noinst_PROGRAMS = test-cache

libtsconfig_a_SOURCES = \
  Errata.cc \
  Errata.h \
  TsErrataUtil.cc \
  TsErrata.h \
  TsBuffer.h \
  NumericType.h \
  IntrusivePtr.h \
  TsBuilder.cc \
  TsBuilder.h \
  TsValue.cc \
  TsValue.h \
  TsConfig.tab.c \
  TsConfig.lex.c

TsConfig.tab.c TsConfig.tab.h: TsConfig.y
	bison -d --name-prefix tsconfig TsConfig.y

TsConfig.lex.c TsConfig.lex.h: TsConfig.flex
	flex --outfile TsConfig.lex.c TsConfig.flex

#test_cache_SOURCES = \
#  wccp-test-cache.cc

#test_cache_LDADD = ./libwccp.a -lconfig++ -lcrypto