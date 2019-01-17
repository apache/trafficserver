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

pkglib_LTLIBRARIES += experimental/uri_signing/uri_signing.la

experimental_uri_signing_uri_signing_la_SOURCES = \
  experimental/uri_signing/uri_signing.c          \
  experimental/uri_signing/config.c               \
  experimental/uri_signing/cookie.c               \
  experimental/uri_signing/jwt.c                  \
  experimental/uri_signing/match.c                \
  experimental/uri_signing/parse.c                \
  experimental/uri_signing/timing.c

experimental_uri_signing_uri_signing_la_LIBADD = @LIBJANSSON@ @LIBCJOSE@ @LIBPCRE@ -lm -lcrypto

check_PROGRAMS += experimental/uri_signing/test_uri_signing

experimental_uri_signing_test_uri_signing_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DURI_SIGNING_UNIT_TEST 
experimental_uri_signing_test_uri_signing_LDADD = @LIBJANSSON@ @LIBCJOSE@ @LIBPCRE@ -lm -lcrypto
experimental_uri_signing_test_uri_signing_SOURCES = \
	experimental/uri_signing/unit_tests/uri_signing_test.cc \
    experimental/uri_signing/jwt.c \
    experimental/uri_signing/common.c \
    experimental/uri_signing/parse.c \
    experimental/uri_signing/cookie.c \
    experimental/uri_signing/config.c \
    experimental/uri_signing/timing.c \
    experimental/uri_signing/match.c
