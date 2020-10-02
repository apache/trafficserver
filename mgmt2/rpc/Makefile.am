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
	jsonrpc/error/RpcError.cc \
	jsonrpc/error/RpcError.h \
	jsonrpc/JsonRpc.cc \
	jsonrpc/JsonRpc.h

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
	@YAMLCPP_LIBS@



###########################################################################################
# RPC server only.
libjsonrpc_server_COMMON = \
	server/RpcServer.cc \
	server/RpcServer.h \
	server/CommBase.cc \
	server/CommBase.h \
	server/LocalUnixSocket.cc \
	server/LocalUnixSocket.h \
	config/JsonRpcConfig.cc \
	config/JsonRpcConfig.h

libjsonrpc_server_la_SOURCES = \
	$(libjsonrpc_server_COMMON)

test_jsonrpcserver_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include \
	@YAMLCPP_INCLUDES@

test_jsonrpcserver_LDFLAGS = \
	@AM_LDFLAGS@

test_jsonrpcserver_SOURCES = \
    server/unit_tests/unit_test_main.cc \
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
	@YAMLCPP_LIBS@


###########################################################################################
# Handlers only

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/mgmt/ \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/

librpcpublichandlers_COMMON = \
	handlers/config/Configuration.cc \
	handlers/config/Configuration.h \
	handlers/records/Records.cc \
	handlers/records/Records.h \
	handlers/metrics/Metrics.cc \
	handlers/metrics/Metrics.h \
	handlers/storage/Storage.h \
	handlers/storage/Storage.cc \
	handlers/server/Server.h \
	handlers/server/Server.cc \
	handlers/plugins/Plugins.h \
	handlers/plugins/Plugins.cc \
	handlers/common/RecordsUtils.cc \
	handlers/common/RecordsUtils.h \
	handlers/Admin.h \
	$(top_builddir)/src/shared/overridable_txn_vars.cc


librpcpublichandlers_la_SOURCES = \
    $(librpcpublichandlers_COMMON)

# TODO: Add test

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)

