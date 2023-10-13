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

include $(top_srcdir)/mk/tidy.mk

noinst_PROGRAMS = CompileParseRules
check_PROGRAMS = test_geometry test_X509HostnameValidator test_tscore

if EXPENSIVE_TESTS
check_PROGRAMS += test_atomic test_freelist
endif

TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=$(abs_top_srcdir)/ci/asan_leak_suppression/unit_tests.txt

BUILT_SOURCES = ParseRules.cc

TESTS = $(check_PROGRAMS)

noinst_LIBRARIES = libtscore.a

AM_CPPFLAGS += \
	@SWOC_INCLUDES@ \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	$(TS_INCLUDES) \
	@HWLOC_INCLUDES@ \
	@YAMLCPP_INCLUDES@

libtscore_a_SOURCES = \
	AcidPtr.cc \
	Arena.cc \
	ArgParser.cc \
	BaseLogFile.cc \
	ConsistentHash.cc \
	ContFlags.cc \
	CryptoHash.cc \
	Diags.cc \
	Encoding.cc \
	Errata.cc \
	EventNotify.cc \
	Extendible.cc \
	Hash.cc \
	HashFNV.cc \
	HashSip.cc \
	HostLookup.cc \
	hugepages.cc \
	ink_args.cc \
	ink_assert.cc \
	ink_base64.cc \
	ink_cap.cc \
	ink_defs.cc \
	InkErrno.cc \
	ink_error.cc \
	ink_file.cc \
	ink_hrtime.cc \
	ink_hw.cc \
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
	JeMiAllocator.cc \
	Layout.cc \
	llqueue.cc \
	lockfile.cc \
	MatcherUtils.cc \
	MMH.cc \
	ParseRules.cc \
	Random.cc \
	Regex.cc \
	Regression.cc \
	runroot.cc \
	signals.cc \
	SourceLocation.cc \
	TextBuffer.cc \
	LogMessage.cc \
	Throttler.cc \
	Tokenizer.cc \
	Version.cc \
	X509HostnameValidator.cc

if HAS_HKDF
if OPENSSL_IS_BORINGSSL
HKDF_impl = HKDF_boringssl.cc
else
if OPENSSL_IS_OPENSSL3
HKDF_impl = HKDF_openssl3.cc
else
HKDF_impl = HKDF_openssl.cc
endif
endif
libtscore_a_SOURCES += \
  $(HKDF_impl)
endif

# Special hacks to generate the parser rules
$(srcdir)/ParseRules.cc: ParseRulesCType

ParseRulesCType: CompileParseRules
	LSAN_OPTIONS='detect_leaks=0' ./CompileParseRules

test_atomic_SOURCES = test_atomic.cc
test_atomic_LDADD = libtscore.a $(top_builddir)/src/api/libtsapicore.a $(top_builddir)/src/tscpp/util/libtscpputil.la @SWOC_LIBS@ @LIBPCRE@ @LIBCAP@

test_freelist_SOURCES = test_freelist.cc
test_freelist_LDADD = libtscore.a  $(top_builddir)/src/api/libtsapicore.a $(top_builddir)/src/tscpp/util/libtscpputil.la @SWOC_LIBS@ @LIBPCRE@ @LIBCAP@

test_geometry_SOURCES = test_geometry.cc
test_geometry_LDADD = libtscore.a  $(top_builddir)/src/api/libtsapicore.a $(top_builddir)/src/tscpp/util/libtscpputil.la @SWOC_LIBS@ @LIBPCRE@ -lstdc++

test_X509HostnameValidator_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/lib/catch2
test_X509HostnameValidator_LDADD = libtscore.a  $(top_builddir)/src/api/libtsapicore.a $(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBCAP@
test_X509HostnameValidator_SOURCES = unit_tests/test_X509HostnameValidator.cc

test_tscore_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_tscore_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)
test_tscore_LDADD = libtscore.a  $(top_builddir)/src/api/libtsapicore.a $(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@ @OPENSSL_LIBS@ @LIBPCRE@ @LIBCAP@
test_tscore_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_AcidPtr.cc \
	unit_tests/test_arena.cc \
	unit_tests/test_ArgParser.cc \
	unit_tests/test_Bravo.cc \
	unit_tests/test_CryptoHash.cc \
	unit_tests/test_Extendible.cc \
	unit_tests/test_Histogram.cc \
	unit_tests/test_History.cc \
	unit_tests/test_ink_inet.cc \
	unit_tests/test_ink_memory.cc \
	unit_tests/test_IntrusivePtr.cc \
	unit_tests/test_layout.cc \
	unit_tests/test_List.cc \
	unit_tests/test_ParseRules.cc \
	unit_tests/test_PluginUserArgs.cc \
	unit_tests/test_PriorityQueue.cc \
	unit_tests/test_Ptr.cc \
	unit_tests/test_Regex.cc \
	unit_tests/test_scoped_resource.cc \
	unit_tests/test_Throttler.cc \
	unit_tests/test_Tokenizer.cc \
	unit_tests/test_Version.cc \
	unit_tests/test_Errata.cc \
	unit_tests/test_MMH.cc \
	unit_tests/test_Random.cc \
	unit_tests/test_Encoding.cc

if HAS_HKDF
test_tscore_SOURCES += \
	unit_tests/test_HKDF.cc
endif

CompileParseRules_SOURCES = CompileParseRules.cc

CompileParseRules$(BUILD_EXEEXT): $(CompileParseRules_OBJECTS)
	$(CXX_FOR_BUILD) $(AM_CXXFLAGS) $(CPPFLAGS) -I$(top_builddir)/include -I$(abs_top_srcdir)/include -o $@ $(abs_top_srcdir)/src/tscore/CompileParseRules.cc

clean-local:
	rm -f ParseRulesCType ParseRulesCTypeToLower ParseRulesCTypeToUpper

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
