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

pkglib_LTLIBRARIES += experimental/slice/slice.la

experimental_slice_slice_la_SOURCES = \
  experimental/slice/client.cc \
  experimental/slice/client.h \
  experimental/slice/ContentRange.cc \
  experimental/slice/ContentRange.h \
  experimental/slice/Data.cc \
  experimental/slice/Data.h \
  experimental/slice/HttpHeader.cc \
  experimental/slice/HttpHeader.h \
  experimental/slice/intercept.cc \
  experimental/slice/intercept.h \
  experimental/slice/Range.cc \
  experimental/slice/Range.h \
  experimental/slice/response.cc \
  experimental/slice/response.h \
  experimental/slice/server.cc \
  experimental/slice/server.h \
  experimental/slice/slice.cc \
  experimental/slice/slice.h \
  experimental/slice/Stage.h \
  experimental/slice/transfer.cc \
  experimental/slice/transfer.h
