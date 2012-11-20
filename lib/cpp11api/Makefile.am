# Makefile.am for C++11 API
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

includedir=$(prefix)/include/ts

AM_CPPFLAGS = -I$(top_builddir)/proxy/api/ts
  
if BUILD_HAVE_CXX_11

if BUILD_HAVE_LIBCXX
AM_CXXFLAGS = -stdlib=libc++
endif

lib_LTLIBRARIES = libatscpp11api.la

libatscpp11api_la_SOURCES = \
  cpp11api.cc
  
include_HEADERS = \
  ts-cpp11.h ts-cpp11-headers.h

endif # BUILD_HAVE_CXX_11
