#
# Makefile.am for yaml-cpp library
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

include $(top_srcdir)/build/tidy.mk

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/lib/yamlcpp/include

noinst_LTLIBRARIES = libyamlcpp.la

libyamlcpp_la_SOURCES = src/binary.cpp \
src/convert.cpp \
src/directives.cpp \
src/emit.cpp \
src/emitfromevents.cpp \
src/emitter.cpp \
src/emitterstate.cpp \
src/emitterutils.cpp \
src/exceptions.cpp \
src/exp.cpp \
src/memory.cpp \
src/node.cpp \
src/node_data.cpp \
src/nodebuilder.cpp \
src/nodeevents.cpp \
src/null.cpp \
src/ostream_wrapper.cpp \
src/parse.cpp \
src/parser.cpp \
src/regex_yaml.cpp \
src/scanner.cpp \
src/scanscalar.cpp \
src/scantag.cpp \
src/scantoken.cpp \
src/simplekey.cpp \
src/singledocparser.cpp \
src/stream.cpp \
src/tag.cpp

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
