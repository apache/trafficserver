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

# Only build if NURAFT_LIBS is set to a non-empty value
pkglib_LTLIBRARIES += experimental/stek_share/stek_share.la

experimental_stek_share_stek_share_la_SOURCES = \
  experimental/stek_share/common.cc \
	experimental/stek_share/common.h \
	experimental/stek_share/log_store.cc \
	experimental/stek_share/log_store.h \
	experimental/stek_share/state_machine.h \
	experimental/stek_share/state_manager.h \
	experimental/stek_share/stek_share.cc \
	experimental/stek_share/stek_share.h \
	experimental/stek_share/stek_utils.cc \
	experimental/stek_share/stek_utils.h

experimental_stek_share_stek_share_la_LDFLAGS = $(AM_LDFLAGS) @YAMLCPP_LDFLAGS@

AM_CPPFLAGS += @NURAFT_INCLUDES@ @YAMLCPP_INCLUDES@

experimental_stek_share_stek_share_la_LIBADD = @NURAFT_LIBS@ @YAMLCPP_LIBS@
