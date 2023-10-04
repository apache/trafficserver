# Makefile.am for traffic/iocore/io_uring
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

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/include/iocore/eventsystem \
	-I$(abs_top_srcdir)/include/iocore/io_uring \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

noinst_LIBRARIES = libinkuring.a

libinkuring_a_SOURCES = \
	IOUringEventIO.cc \
	IOUringEventIO.h \
	io_uring.cc \
	I_IO_URING.h \
	P_IO_URING.h


check_PROGRAMS = test_diskIO

test_LD_FLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@

test_CPP_FLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/lib/catch2

test_LD_ADD = \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/iocore/io_uring/libinkuring.a \
	@HWLOC_LIBS@


test_diskIO_SOURCES = unit_tests/test_diskIO.cc
test_diskIO_CPPFLAGS = $(test_CPP_FLAGS)
test_diskIO_LDFLAGS = $(test_LD_FLAGS)
test_diskIO_LDADD = $(test_LD_ADD)
