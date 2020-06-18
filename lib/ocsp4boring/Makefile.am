#
# Makefile.am for ocsp library
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

AM_CPPFLAGS += -I$(abs_top_srcdir)/lib/ocsp4boring

noinst_LTLIBRARIES = libocsp4boring.la

libocsp4boring_la_SOURCES = ocsp_asn.c \
ocsp_cl.c \
ocsp_lib.c \
ocsp_prn.c \
ocsp_vfy.c \
ocsp_srv.c \
ocsp_ht.c \
ocsp_ext.c

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
