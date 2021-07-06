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

pkglib_LTLIBRARIES += header_rewrite/header_rewrite.la

noinst_LTLIBRARIES += header_rewrite/parser.la

header_rewrite_header_rewrite_la_SOURCES = \
	header_rewrite/condition.cc \
	header_rewrite/condition.h \
	header_rewrite/conditions.cc \
	header_rewrite/conditions.h \
	header_rewrite/factory.cc \
	header_rewrite/factory.h \
	header_rewrite/header_rewrite.cc \
	header_rewrite/lulu.cc \
	header_rewrite/lulu.h \
	header_rewrite/matcher.h \
	header_rewrite/operator.cc \
	header_rewrite/operator.h \
	header_rewrite/operators.cc \
	header_rewrite/operators.h \
	header_rewrite/regex_helper.cc \
	header_rewrite/regex_helper.h \
	header_rewrite/resources.cc \
	header_rewrite/resources.h \
	header_rewrite/ruleset.cc \
	header_rewrite/ruleset.h \
	header_rewrite/statement.cc \
	header_rewrite/statement.h \
	header_rewrite/value.cc \
	header_rewrite/value.h

if HAS_MAXMINDDB
header_rewrite_header_rewrite_la_SOURCES += header_rewrite/conditions_geo_maxmind.cc
endif

if HAS_GEOIP
header_rewrite_header_rewrite_la_SOURCES += header_rewrite/conditions_geo_geoip.cc
endif

header_rewrite_parser_la_SOURCES = \
	header_rewrite/parser.cc \
	header_rewrite/parser.h

header_rewrite_header_rewrite_la_LIBADD = \
	header_rewrite/parser.la

if HAS_GEOIP
header_rewrite_header_rewrite_la_LIBADD += $(GEOIP_LIBS)
endif

if HAS_MAXMINDDB
header_rewrite_header_rewrite_la_LIBADD += $(MAXMINDDB_LIBS)
endif

check_PROGRAMS += header_rewrite/header_rewrite_test
header_rewrite_header_rewrite_test_SOURCES = \
	header_rewrite/header_rewrite_test.cc
header_rewrite_header_rewrite_test_LDADD = \
	header_rewrite/parser.la
if HAS_GEOIP
header_rewrite_header_rewrite_test_LDADD += $(GEOIP_LIBS)
endif

if HAS_MAXMINDDB
header_rewrite_header_rewrite_test_LDADD += $(MAXMINDDB_LIBS)
endif
