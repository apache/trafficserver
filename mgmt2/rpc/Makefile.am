#
# Makefile.am for the RPC/jsonrpc module
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
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/iocore/utils \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib/ \
	-I$(abs_top_srcdir)/mgmt2/ \
	$(TS_INCLUDES) \
	@YAMLCPP_INCLUDES@


noinst_LTLIBRARIES = libjsonrpc_protocol.la libjsonrpc_server.la librpcpublichandlers.la
check_PROGRAMS = test_jsonrpc test_jsonrpcserver


TESTS = $(check_PROGRAMS)

# TODO: Remove libmgmt_p.la This can only be removed once we remove the ProcessManager dependency and the librecords
#       Message stuff between TM and TS
# TODO: handlers - mgmt/utils needed as ProcessManager.h is included in many pleaces, we should be able to remove it once
#       we move away from TM

###########################################################################################
# Protocol library only, no transport.

libjsonrpc_protocol_COMMON = \
	jsonrpc/error/RPCError.cc \
	jsonrpc/error/RPCError.h \
	jsonrpc/JsonRPCManager.cc \
	jsonrpc/JsonRPCManager.h

libjsonrpc_protocol_la_SOURCES = \
	$(libjsonrpc_protocol_COMMON)


test_jsonrpc_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include \
	@YAMLCPP_INCLUDES@

test_jsonrpc_LDFLAGS = \
	@AM_LDFLAGS@

test_jsonrpc_SOURCES = \
	jsonrpc/unit_tests/unit_test_main.cc \
	jsonrpc/unit_tests/test_basic_protocol.cc

# TODO: Remove libmgmt_p.la

test_jsonrpc_LDADD = \
	libjsonrpc_protocol.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@YAMLCPP_LIBS@ @HWLOC_LIBS@



###########################################################################################
# RPC server only.
libjsonrpc_server_COMMON = \
	server/RPCServer.cc \
	server/RPCServer.h \
	server/CommBase.cc \
	server/CommBase.h \
	server/IPCSocketServer.cc \
	server/IPCSocketServer.h \
	config/JsonRPCConfig.cc \
	config/JsonRPCConfig.h

libjsonrpc_server_la_SOURCES = \
	$(libjsonrpc_server_COMMON)

test_jsonrpcserver_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include \
	-I$(abs_top_srcdir)/tests \
	@YAMLCPP_INCLUDES@

test_jsonrpcserver_LDFLAGS = \
	@AM_LDFLAGS@

test_jsonrpcserver_SOURCES = \
	server/unit_tests/unit_test_main.cc \
	$(shared_rpc_ipc_client_SOURCES) \
	server/unit_tests/test_rpcserver.cc

test_jsonrpcserver_LDADD = \
	libjsonrpc_protocol.la \
	libjsonrpc_server.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@YAMLCPP_LIBS@ @HWLOC_LIBS@


###########################################################################################
# Handlers only

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/mgmt/ \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/

librpcpublichandlers_COMMON = \
	handlers/common/RecordsUtils.cc \
	handlers/common/RecordsUtils.h \
	handlers/config/Configuration.cc \
	handlers/config/Configuration.h \
	handlers/records/Records.cc \
	handlers/records/Records.h \
	handlers/storage/Storage.h \
	handlers/storage/Storage.cc \
	handlers/server/Server.h \
	handlers/server/Server.cc \
	handlers/plugins/Plugins.h \
	handlers/plugins/Plugins.cc \
	handlers/Admin.h

librpcpublichandlers_la_SOURCES = \
    $(librpcpublichandlers_COMMON) \
    $(shared_overridable_txn_vars_SOURCES)


# distclean
# This is a workaround to deal with a newer version of automake, apparently there
# is an issue when including subdir-objects and sources outside of subtree.
# If we include a file from another subdir as is(was) the case of overridable_txn,
# then the distclean will try to remove the file from the original folder as well
# from here. To overcome this issue, we create a file here that will be used
# for building.
# We have also added a proper cleaning for it.
shared_overridable_txn_vars_SOURCES = overridable_txn_vars.cc
nodist_librpcpublichandlers_la_SOURCES = $(shared_overridable_txn_vars_SOURCES)

shared_rpc_ipc_client_SOURCES = IPCSocketClient.cc

# This may not be needed. Ok for now.
CLEANDIST = $(shared_overridable_txn_vars_SOURCES) $(shared_rpc_ipc_client_SOURCES)

clean-local:
	rm -f $(shared_overridable_txn_vars_SOURCES) $(shared_rpc_ipc_client_SOURCES)

distclean-local:
	rm -f $(shared_overridable_txn_vars_SOURCES) $(shared_rpc_ipc_client_SOURCES)

# Build with this file instead of the original one.
$(shared_overridable_txn_vars_SOURCES):
	echo "#include \"$(top_builddir)/src/shared/$@\"" >$@

$(shared_rpc_ipc_client_SOURCES):
	echo "#include \"$(top_builddir)/src/shared/rpc/$@\"" >$@

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)

