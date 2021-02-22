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

pkglib_LTLIBRARIES += experimental/client_allow_list/client_allow_list.la

experimental_client_allow_list_client_allow_list_la_SOURCES = \
experimental/client_allow_list/main.cc \
experimental/client_allow_list/util.cc

experimental_client_allow_list_client_allow_list_la_LDFLAGS = $(AM_LDFLAGS)

AM_CPPFLAGS += @YAMLCPP_INCLUDES@

check_PROGRAMS += experimental/client_allow_list/test_client_allow_list

experimental_client_allow_list_test_client_allow_list_CPPFLAGS = \
$(AM_CPPFLAGS) -Iexperimental/client_allow_list -DCLIENT_ALLOW_LIST_UNIT_TEST

experimental_client_allow_list_test_client_allow_list_SOURCES = \
experimental/client_allow_list/unit_tests/test.cc \
experimental/client_allow_list/util.cc

experimental_client_allow_list_test_client_allow_list_LDFLAGS = @LIBPCRE@ @YAMLCPP_LDFLAGS@

experimental_client_allow_list_test_client_allow_list_LDADD = @YAMLCPP_LIBS@
