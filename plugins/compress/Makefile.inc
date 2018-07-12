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

pkglib_LTLIBRARIES += compress/compress.la
compress_compress_la_SOURCES = compress/compress.cc compress/configuration.cc compress/misc.cc

compress_compress_la_LDFLAGS = \
  $(AM_LDFLAGS) $(BROTLIENC_LIB)

compress_compress_la_CXXFLAGS = $(AM_CXXFLAGS) $(BROTLIENC_CFLAGS)
