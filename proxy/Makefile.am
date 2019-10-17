# proxy Makefile.am
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

SUBDIRS = hdrs shared http http2 logging
if ENABLE_QUIC
SUBDIRS += http3
endif

noinst_LIBRARIES = libproxy.a

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_srcdir)/http \
	-I$(abs_srcdir)/http2 \
	-I$(abs_srcdir)/http3 \
	-I$(abs_srcdir)/logging \
	-I$(abs_srcdir)/http/remap \
	-I$(abs_srcdir)/hdrs \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/lib/yamlcpp/include \
	$(TS_INCLUDES)

noinst_HEADERS = \
	Show.h

libproxy_a_SOURCES = \
	CacheControl.cc \
	CacheControl.h \
	ControlBase.cc \
	ControlBase.h \
	ControlMatcher.cc \
	ControlMatcher.h \
	IPAllow.cc \
	IPAllow.h \
	ParentConsistentHash.cc \
	ParentConsistentHash.h \
	ParentRoundRobin.cc \
	ParentRoundRobin.h \
	ParentSelectionStrategy.cc \
	ParentSelection.cc \
	ParentSelection.h \
	Plugin.cc \
	Plugin.h \
	PluginVC.cc \
	PluginVC.h \
	ProtocolProbeSessionAccept.cc \
	ProtocolProbeSessionAccept.h \
	ProxySession.cc \
	ProxySession.h \
	ProxyTransaction.cc \
	ProxyTransaction.h \
	ReverseProxy.cc \
	ReverseProxy.h \
	StatPages.cc \
	StatPages.h \
	Transform.cc \
	Transform.h

if BUILD_TESTS
libproxy_a_SOURCES += \
	RegressionSM.h \
	RegressionSM.cc
endif


# These are currently built separate, as part of building the lib/ tree, using
# the normal LuaJIT build system. We are using the .o's directly, instead of the
# luajit.a to avoid the linker from optimizing symbols away. We could maybe
# switch to using the luajit.so, but that involves making sure it installs safely
# and cleanly.
LUAJIT = \
	lib_aux.o \
	lib_base.o \
	lib_bit.o \
	lib_debug.o \
	lib_ffi.o \
	lib_init.o \
	lib_io.o \
	lib_jit.o \
	lib_math.o \
	lib_os.o \
	lib_package.o \
	lib_string.o \
	lib_table.o \
	lj_alloc.o \
	lj_api.o \
	lj_asm.o \
	lj_bc.o \
	lj_bcread.o \
	lj_bcwrite.o \
	lj_carith.o \
	lj_ccall.o \
	lj_ccallback.o \
	lj_cconv.o \
	lj_cdata.o \
	lj_char.o \
	lj_clib.o \
	lj_cparse.o \
	lj_crecord.o \
	lj_ctype.o \
	lj_debug.o \
	lj_dispatch.o \
	lj_err.o \
	lj_ffrecord.o \
	lj_func.o \
	lj_gc.o \
	lj_gdbjit.o \
	lj_ir.o \
	lj_lex.o \
	lj_lib.o \
	lj_load.o \
	lj_mcode.o \
	lj_meta.o \
	lj_obj.o \
	lj_opt_dce.o \
	lj_opt_fold.o \
	lj_opt_loop.o \
	lj_opt_mem.o \
	lj_opt_narrow.o \
	lj_opt_sink.o \
	lj_opt_split.o \
	lj_parse.o \
	lj_record.o \
	lj_snap.o \
	lj_state.o \
	lj_str.o \
	lj_strscan.o \
	lj_tab.o \
	lj_trace.o \
	lj_udata.o \
	lj_vm.o \
	lj_vmevent.o \
	lj_vmmath.o

EXTRA_DIST = example_alarm_bin.sh example_prep.sh

install-data-local:
	if [ `id -un` != "root" ]; then \
		$(INSTALL) -d $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgruntimedir) \
				$(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgcachedir); \
	else \
		 $(INSTALL) -d $(DESTDIR)$(pkglocalstatedir); \
		 $(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkglogdir) \
				$(DESTDIR)$(pkgruntimedir) $(DESTDIR)$(pkgsysconfdir) \
				$(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgcachedir); \
	fi

install-data-hook:
	if [ `id -un` == "root" ]; then \
		chown -R $(pkgsysuser):$(pkgsysgroup) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgdatadir);\
	fi
	-echo "<TS_VERSION> $(PACKAGE_VERSION)" > $(DESTDIR)$(pkgsysconfdir)/trafficserver-release

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
