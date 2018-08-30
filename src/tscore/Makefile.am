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

library_includedir=$(includedir)/ts

noinst_PROGRAMS = mkdfa CompileParseRules
check_PROGRAMS = test_libtscore test_arena test_atomic test_freelist test_geometry test_List test_Map test_Vec test_X509HostnameValidator test_tscore

TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=suppression.txt

TESTS = $(check_PROGRAMS)

lib_LTLIBRARIES = libtscore.la

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/include/records \
	$(TS_INCLUDES) \
	@YAMLCPP_INCLUDES@

libtscore_la_LDFLAGS = -no-undefined -version-info @TS_LIBTOOL_VERSION@ @YAMLCPP_LDFLAGS@
libtscore_la_LIBADD = \
	@HWLOC_LIBS@ \
	@LIBOBJS@ \
	@LIBPCRE@ \
	@OPENSSL_LIBS@ \
	@LIBTCL@ \
	@LIBRESOLV@ \
	@LIBCAP@ \
	@YAMLCPP_LIBS@ \
	-lc

libtscore_la_SOURCES = \
	Allocator.h \
	AcidPtr.cc \
	AcidPtr.h \
	Arena.cc \
	Arena.h \
	BaseLogFile.cc \
	BaseLogFile.h \
	Bitops.cc \
	Bitops.h \
	BufferWriter.h \
	BufferWriterForward.h \
	BufferWriterFormat.cc \
	ConsistentHash.cc \
	ConsistentHash.h \
	ContFlags.cc \
	ContFlags.h \
	CryptoHash.cc \
	CryptoHash.h \
	defalloc.h \
	Diags.cc \
	Diags.h \
	DynArray.h \
	EventNotify.cc \
	EventNotify.h \
	Extendible.h \
	fastlz.c \
	fastlz.h \
	Hash.cc \
	HashFNV.cc \
	HashFNV.h \
	Hash.h \
	HashMD5.cc \
	HashMD5.h \
	HashSip.cc \
	HashSip.h \
	History.h \
	HostLookup.cc \
	HostLookup.h \
	hugepages.cc \
	hugepages.h \
	I_Layout.h \
	ink_aiocb.h \
	ink_align.h \
	ink_apidefs.h \
	ink_args.cc \
	ink_args.h \
	ink_assert.cc \
	ink_assert.h \
	ink_atomic.h \
	ink_base64.cc \
	ink_base64.h \
	ink_cap.cc \
	ink_cap.h \
	ink_code.cc \
	ink_code.h \
	ink_defs.cc \
	ink_defs.h \
	InkErrno.cc \
	InkErrno.h \
	ink_error.cc \
	ink_error.h \
	ink_exception.h \
	ink_file.cc \
	ink_file.h \
	ink_hash_table.cc \
	ink_hash_table.h \
	ink_hrtime.cc \
	ink_hrtime.h \
	ink_inet.cc \
	ink_inet.h \
	ink_inout.h \
	ink_llqueue.h \
	ink_lockfile.h \
	INK_MD5.h \
	ink_memory.cc \
	ink_memory.h \
	ink_mutex.cc \
	ink_mutex.h \
	ink_platform.h \
	ink_queue.cc \
	ink_queue.h \
	ink_queue_utils.cc \
	ink_rand.cc \
	ink_rand.h \
	ink_res_init.cc \
	ink_res_mkquery.cc \
	ink_resolver.h \
	ink_resource.cc \
	ink_resource.h \
	ink_rwlock.cc \
	ink_rwlock.h \
	ink_sock.cc \
	ink_sock.h \
	ink_sprintf.cc \
	ink_sprintf.h \
	ink_stack_trace.cc \
	ink_stack_trace.h \
	ink_string.cc \
	ink_string++.cc \
	ink_string.h \
	ink_string++.h \
	ink_sys_control.cc \
	ink_sys_control.h \
	ink_syslog.cc \
	ink_syslog.h \
	ink_thread.cc \
	ink_thread.h \
	ink_time.cc \
	ink_time.h \
	ink_uuid.cc \
	ink_uuid.h \
	IntrusiveDList.h \
	IpMap.cc \
	IpMapConf.cc \
	IpMapConf.h \
	IpMap.h \
	I_Version.h \
	JeAllocator.h \
	JeAllocator.cc \
	Layout.cc \
	List.h \
	llqueue.cc \
	lockfile.cc \
	Map.h \
	MatcherUtils.cc \
	MatcherUtils.h \
	MemSpan.h \
	MemArena.cc \
	MemArena.h \
	MMH.cc \
	MMH.h \
	MT_hashtable.h \
	ParseRules.cc \
	ParseRules.h \
	PriorityQueue.h \
	Ptr.h \
	RawHashTable.cc \
	RawHashTable.h \
	RbTree.cc \
	RbTree.h \
	Regex.cc \
	Regex.h \
	Regression.cc \
	Regression.h \
	Result.h \
	runroot.cc \
	runroot.h \
	signals.cc \
	signals.h \
	SimpleTokenizer.h \
	SourceLocation.cc \
	SourceLocation.h \
	TestBox.h \
	TextBuffer.cc \
	TextBuffer.h \
	Tokenizer.cc \
	Tokenizer.h \
	Trie.h \
	TsBuffer.h \
	Version.cc \
	X509HostnameValidator.cc \
	X509HostnameValidator.h

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
test_atomic_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_freelist_SOURCES = test_freelist.cc
test_freelist_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_arena_SOURCES = test_arena.cc
test_arena_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_List_SOURCES = test_List.cc
test_Map_SOURCES = test_Map.cc
test_Map_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_Vec_SOURCES = test_Vec.cc
test_Vec_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_geometry_SOURCES = test_geometry.cc
test_geometry_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@

test_X509HostnameValidator_SOURCES = test_X509HostnameValidator.cc
test_X509HostnameValidator_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@ @OPENSSL_LIBS@

test_libtscore_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la @LIBTCL@ @LIBPCRE@
test_libtscore_SOURCES = \
	test_PriorityQueue.cc \
	tests.cc

test_tscore_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include

test_tscore_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)
test_tscore_LDADD = libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la $(top_builddir)/iocore/eventsystem/libinkevent.a
test_tscore_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_AcidPtr.cc \
	unit_tests/test_BufferWriter.cc \
	unit_tests/test_BufferWriterFormat.cc \
	unit_tests/test_Extendible.cc \
	unit_tests/test_History.cc \
	unit_tests/test_ink_inet.cc \
	unit_tests/test_IntrusiveDList.cc \
	unit_tests/test_IntrusiveHashMap.cc \
	unit_tests/test_IntrusivePtr.cc \
	unit_tests/test_IpMap.cc \
	unit_tests/test_layout.cc \
	unit_tests/test_MemSpan.cc \
	unit_tests/test_MemArena.cc \
	unit_tests/test_MT_hashtable.cc \
	unit_tests/test_Ptr.cc \
	unit_tests/test_Regex.cc \
	unit_tests/test_Scalar.cc \
	unit_tests/test_scoped_resource.cc

CompileParseRules_SOURCES = CompileParseRules.cc

clean-local:
	rm -f ParseRulesCType ParseRulesCTypeToLower ParseRulesCTypeToUpper

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
