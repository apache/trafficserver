#
# Makefile.am for the Enterprise Management module.
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

bin_PROGRAMS += traffic_ctl_jsonrpc/traffic_ctl

traffic_ctl_jsonrpc_traffic_ctl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(TS_INCLUDES) \
	-I$(abs_top_srcdir)/mgmt2 \
    -I$(abs_top_srcdir)/tests \
	@YAMLCPP_INCLUDES@

traffic_ctl_jsonrpc_traffic_ctl_SOURCES = \
	traffic_ctl_jsonrpc/traffic_ctl.cc \
	traffic_ctl_jsonrpc/CtrlPrinters.cc \
	traffic_ctl_jsonrpc/CtrlCommands.cc

traffic_ctl_jsonrpc_traffic_ctl_LDADD = \
    $(top_builddir)/src/tscore/libtscore.la \
    @HWLOC_LIBS@ @YAMLCPP_LIBS@
