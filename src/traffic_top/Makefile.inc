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

# We only build traffic_top if libcurses and libcurl are available.
if BUILD_TRAFFIC_TOP

bin_PROGRAMS += traffic_top/traffic_top

traffic_top_traffic_top_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/include \
	$(TS_INCLUDES) \
	@CURL_CFLAGS@ \
	@CURSES_CFLAGS@ \
	@YAMLCPP_INCLUDES@

traffic_top_traffic_top_LDFLAGS = \
	$(AM_LDFLAGS) \
	@CURSES_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LIBS@


traffic_top_traffic_top_SOURCES = \
	traffic_top/traffic_top.cc \
	shared/rpc/IPCSocketClient.cc

traffic_top_traffic_top_LDADD = \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@CURL_LIBS@ \
	@CURSES_LIBS@ \
	@HWLOC_LIBS@ \
	@YAMLCPP_LIBS@
endif
