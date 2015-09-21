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

include $(top_srcdir)/build/plugins.mk

pkglib_LTLIBRARIES = mysql_remap.la
noinst_LTLIBRARIES = libmysql_remap.la

AM_CPPFLAGS += \
  -I$(srcdir)/lib

mysql_remap_la_SOURCES = mysql_remap.cc

libmysql_remap_la_SOURCES = \
  lib/dictionary.c \
  lib/iniparser.c

mysql_remap_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS) $(LIB_MYSQLCLIENT)
mysql_remap_la_LIBADD = libmysql_remap.la
