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

include $(top_srcdir)/build/plugins.mk

AM_CPPFLAGS += -DPLUGIN_TAG=\"inliner\"

pkglib_LTLIBRARIES = inliner.la

inliner_la_SOURCES = \
  ats-inliner.cc \
  cache.cc \
  chunk-decoder.cc \
  fetcher.cc \
  html-parser.cc \
  inliner-handler.cc \
  ts.cc

inliner_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

