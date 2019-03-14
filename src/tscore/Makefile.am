# libts Makefile.am
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

include $(top_srcdir)/build/tidy.mk

noinst_PROGRAMS = mkdfa CompileParseRules
check_PROGRAMS = test_atomic test_freelist test_geometry test_X509HostnameValidator test_tscore

TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=suppression.txt

TESTS = $(check_PROGRAMS)

lib_LTLIBRARIES = libtscore.la

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/include/records \
	-I$(abs_top_srcdir)/lib \
	$(TS_INCLUDES) \
	@YAMLCPP_INCLUDES@

libtscore_la_LDFLAGS = -no-undefined -version-info @TS_LIBTOOL_VERSION@ @YAMLCPP_LDFLAGS@
libtscore_la_LIBADD = \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@HWLOC_LIBS@ \
	@LIBOBJS@ \
	@LIBPCRE@ \
	@OPENSSL_LIBS@ \
	@LIBRESOLV@ \
	@LIBCAP@ \
	@YAMLCPP_LIBS@ \
	-lc

libtscore_la_SOURCES = \
	AcidPtr.cc \
	Arena.cc \
	ArgParser.cc \
	BaseLogFile.cc \
	BufferWriterFormat.cc \
	ConsistentHash.cc \
	ContFlags.cc \
	CryptoHash.cc \
	Diags.cc \
	EventNotify.cc \
	fastlz.c \
	Hash.cc \
	HashFNV.cc \
	HashMD5.cc \
	HashSip.cc \
	HostLookup.cc \
	hugepages.cc \
	ink_args.cc \
	ink_assert.cc \
	ink_base64.cc \
	ink_cap.cc \
	ink_code.cc \
	ink_defs.cc \
	InkErrno.cc \
	ink_error.cc \
	ink_file.cc \
	ink_hrtime.cc \
	ink_inet.cc \
	ink_memory.cc \
	ink_mutex.cc \
	ink_queue.cc \
	ink_queue_utils.cc \
	ink_rand.cc \
	ink_res_init.cc \
	ink_res_mkquery.cc \
	ink_resource.cc \
	ink_rwlock.cc \
	ink_sock.cc \
	ink_sprintf.cc \
	ink_stack_trace.cc \
	ink_string.cc \
	ink_string++.cc \
	ink_sys_control.cc \
	ink_syslog.cc \
	ink_thread.cc \
	ink_time.cc \
	ink_uuid.cc \
	IpMap.cc \
	IpMapConf.cc \
	JeAllocator.cc \
	Layout.cc \
	llqueue.cc \
	lockfile.cc \
	MatcherUtils.cc \
	MemArena.cc \
	MMH.cc \
	ParseRules.cc \
	RbTree.cc \
	Regex.cc \
	Regression.cc \
	runroot.cc \
	signals.cc \
	SourceLocation.cc \
	TextBuffer.cc \
	Tokenizer.cc \
	ts_file.cc \
	Version.cc \
	X509HostnameValidator.cc

BufferWriterFormat.o : AM_CPPFLAGS += -Wno-char-subscripts

#test_UNUSED_SOURCES = \
#  load_http_hdr.cc \
#  IntrusivePtrTest.cc \
#  IpMapTest.cc \
#  TestHttpHeader.cc

# Special hacks to generate the parser rules
$(srcdir)/ParseRules.cc: ParseRulesCType

ParseRulesCType: CompileParseRules
	LSAN_OPTIONS='detect_leaks=0' ./CompileParseRules

mkdfa_SOURCES = mkdfa.c

test_atomic_SOURCES = test_atomic.cc
test_atomic_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBPCRE@

test_freelist_SOURCES = test_freelist.cc
test_freelist_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBPCRE@

test_geometry_SOURCES = test_geometry.cc
test_geometry_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBPCRE@

test_X509HostnameValidator_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include
test_X509HostnameValidator_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBPCRE@ @OPENSSL_LIBS@
test_X509HostnameValidator_SOURCES = unit_tests/test_X509HostnameValidator.cc

test_tscore_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include

test_tscore_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)
test_tscore_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la $(top_builddir)/iocore/eventsystem/libinkevent.a
test_tscore_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_AcidPtr.cc \
	unit_tests/test_arena.cc \
	unit_tests/test_ArgParser.cc \
	unit_tests/test_BufferWriter.cc \
	unit_tests/test_BufferWriterFormat.cc \
	unit_tests/test_Extendible.cc \
	unit_tests/test_History.cc \
	unit_tests/test_ink_inet.cc \
	unit_tests/test_IntrusiveHashMap.cc \
	unit_tests/test_IntrusivePtr.cc \
	unit_tests/test_IpMap.cc \
	unit_tests/test_layout.cc \
	unit_tests/test_List.cc \
	unit_tests/test_MemArena.cc \
	unit_tests/test_MT_hashtable.cc \
	unit_tests/test_PriorityQueue.cc \
	unit_tests/test_Ptr.cc \
	unit_tests/test_Regex.cc \
	unit_tests/test_Scalar.cc \
	unit_tests/test_scoped_resource.cc \
	unit_tests/test_ts_file.cc

CompileParseRules_SOURCES = CompileParseRules.cc

clean-local:
	rm -f ParseRulesCType ParseRulesCTypeToLower ParseRulesCTypeToUpper

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
