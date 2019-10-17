# Makefile.am for the traffic/iocore/eventsystem hierarchy
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
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	$(TS_INCLUDES)

TESTS = $(check_PROGRAMS)

noinst_LIBRARIES = libinkevent.a

libinkevent_a_SOURCES = \
	EventSystem.cc \
	IOBuffer.cc \
	I_Action.h \
	I_Continuation.h \
	I_EThread.h \
	I_Event.h \
	I_EventProcessor.h \
	I_EventSystem.h \
	I_IOBuffer.h \
	I_Lock.h \
	I_PriorityEventQueue.h \
	I_Processor.h \
	I_ProtectedQueue.h \
	I_ProxyAllocator.h \
	I_SocketManager.h \
	I_Tasks.h \
	I_Thread.h \
	I_VConnection.h \
	I_VIO.h \
	Inline.cc \
	Lock.cc \
	MIOBufferWriter.cc \
	PQ-List.cc \
	P_EventSystem.h \
	P_Freer.h \
	P_IOBuffer.h \
	P_ProtectedQueue.h \
	P_Thread.h \
	P_UnixEThread.h \
	P_UnixEvent.h \
	P_UnixEventProcessor.h \
	P_UnixSocketManager.h \
	P_VConnection.h \
	P_VIO.h \
	Processor.cc \
	ProtectedQueue.cc \
	ProxyAllocator.cc \
	SocketManager.cc \
	Tasks.cc \
	Thread.cc \
	UnixEThread.cc \
	UnixEvent.cc \
	UnixEventProcessor.cc

check_PROGRAMS = test_IOBuffer \
	test_EventSystem \
	test_MIOBufferWriter

test_LD_FLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@

test_CPP_FLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/tests/include \
	@OPENSSL_INCLUDES@

test_LD_ADD = \
	libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@HWLOC_LIBS@

test_EventSystem_SOURCES = unit_tests/test_EventSystem.cc
test_EventSystem_CPPFLAGS = $(test_CPP_FLAGS)
test_EventSystem_LDFLAGS = $(test_LD_FLAGS)
test_EventSystem_LDADD = $(test_LD_ADD)

test_IOBuffer_SOURCES = unit_tests/test_IOBuffer.cc
test_IOBuffer_CPPFLAGS = $(test_CPP_FLAGS)
test_IOBuffer_LDFLAGS = $(test_LD_FLAGS)
test_IOBuffer_LDADD = $(test_LD_ADD)

test_MIOBufferWriter_SOURCES = unit_tests/test_MIOBufferWriter.cc
test_MIOBufferWriter_CPPFLAGS = $(test_CPP_FLAGS)
test_MIOBufferWriter_LDFLAGS = $(test_LD_FLAGS)

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
