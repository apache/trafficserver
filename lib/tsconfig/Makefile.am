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

noinst_LIBRARIES = libtsconfig.a

libtsconfig_a_SOURCES = \
  TsConfig.tab.c TsConfig.tab.h \
  TsConfig.lex.c TsConfig.lex.h \
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
  TsValue.h

TsConfig.tab.c TsConfig.tab.h: TsConfig.y
	$(YACC) -d -b TsConfig --name-prefix tsconfig TsConfig.y

TsConfig.lex.c TsConfig.lex.h: TsConfig.flex
	$(LEX) --outfile TsConfig.lex.c TsConfig.flex
