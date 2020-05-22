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

sslheaders_CPP_FLAGS = \
	$(AM_CPPFLAGS) \
	@OPENSSL_INCLUDES@ \
	-I$(abs_top_srcdir)/include

noinst_LTLIBRARIES += experimental/sslheaders/libsslhdr.la
pkglib_LTLIBRARIES += experimental/sslheaders/sslheaders.la
check_PROGRAMS += experimental/sslheaders/test_sslheaders

experimental_sslheaders_libsslhdr_la_SOURCES = \
	experimental/sslheaders/sslheaders.h \
	experimental/sslheaders/expand.cc \
	experimental/sslheaders/util.cc
experimental_sslheaders_libsslhdr_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	@OPENSSL_INCLUDES@

experimental_sslheaders_sslheaders_la_SOURCES = \
	experimental/sslheaders/sslheaders.cc \
	experimental/sslheaders/sslheaders.h
experimental_sslheaders_sslheaders_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	@OPENSSL_INCLUDES@
experimental_sslheaders_sslheaders_la_LIBADD = \
        experimental/sslheaders/libsslhdr.la

experimental_sslheaders_test_sslheaders_SOURCES = \
	experimental/sslheaders/unit_tests/unit_test_main.cc \
	experimental/sslheaders/unit_tests/test_sslheaders.cc \
	experimental/sslheaders/sslheaders.h
experimental_sslheaders_test_sslheaders_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	@OPENSSL_INCLUDES@ \
	-I$(abs_top_srcdir)/tests/include \
	-I$(abs_top_srcdir)/include
experimental_sslheaders_test_sslheaders_LDFLAGS = @OPENSSL_LDFLAGS@
experimental_sslheaders_test_sslheaders_LDADD = \
	@AM_LDFLAGS@ \
	experimental/sslheaders/libsslhdr.la \
	$(abs_top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@OPENSSL_LIBS@

# vim: ft=make ts=8 sw=8 et:
