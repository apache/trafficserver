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

tsmemcache_la_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts

pkglib_LTLIBRARIES = tsmemcache.la

tsmemcache_la_SOURCES = \
  tsmemcache.cc

tsmemcache_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
