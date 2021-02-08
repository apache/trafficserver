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

bin_PROGRAMS += traffic_quic/traffic_quic

traffic_quic_traffic_quic_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/lib/records \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/proxy/http3 \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/proxy/shared \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@ @YAMLCPP_INCLUDES@

traffic_quic_traffic_quic_LDFLAGS = \
	$(AM_LDFLAGS) \
	@OPENSSL_LDFLAGS@ \
	@BORINGOCSP_LDFLAGS@

traffic_quic_traffic_quic_SOURCES = \
	traffic_quic/quic_client.cc \
	traffic_quic/traffic_quic.cc

traffic_quic_traffic_quic_LDADD = \
	$(top_builddir)/iocore/net/libinknet.a \
	$(top_builddir)/iocore/aio/libinkaio.a \
	$(top_builddir)/iocore/net/quic/libquic.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	$(top_builddir)/proxy/http3/libhttp3.a \
	$(top_builddir)/proxy/http2/libhttp2.a \
	$(top_builddir)/proxy/libproxy.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	@HWLOC_LIBS@ \
	@YAMLCPP_LIBS@ \
	@OPENSSL_LIBS@ \
	@BORINGOCSP_LIBS@ \
	@LIBPCRE@

