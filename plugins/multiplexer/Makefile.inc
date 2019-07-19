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

multiplexer_multiplexer_la_CPPFLAGS = $(AM_CPPFLAGS) -DPLUGIN_TAG=\"multiplexer\"

pkglib_LTLIBRARIES += multiplexer/multiplexer.la

multiplexer_multiplexer_la_SOURCES = \
  multiplexer/ats-multiplexer.cc \
  multiplexer/chunk-decoder.cc \
  multiplexer/dispatch.cc \
  multiplexer/fetcher.cc \
  multiplexer/original-request.cc \
  multiplexer/post.cc \
  multiplexer/ts.cc
