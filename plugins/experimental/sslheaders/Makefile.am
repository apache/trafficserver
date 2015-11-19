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

AM_CPPFLAGS += @OPENSSL_INCLUDES@ -I$(top_srcdir)/lib

noinst_LTLIBRARIES = libsslhdr.la
pkglib_LTLIBRARIES = sslheaders.la
check_PROGRAMS = test_sslheaders

libsslhdr_la_SOURCES = sslheaders.h expand.cc util.cc

sslheaders_la_SOURCES = sslheaders.cc sslheaders.h
sslheaders_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
sslheaders_la_LIBADD = libsslhdr.la

test_sslheaders_SOURCES = test_sslheaders.cc sslheaders.h
test_sslheaders_LDFLAGS = @OPENSSL_LDFLAGS@
test_sslheaders_LDADD = \
  @LIBTOOL_LINK_FLAGS@ \
  libsslhdr.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  @OPENSSL_LIBS@

TESTS = $(check_PROGRAMS)
