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

# Add new plugins in alphabetic order, but keep experimental always at the bottom.

include $(top_srcdir)/build/plugins.mk
include $(top_srcdir)/build/tidy.mk

check_PROGRAMS =
noinst_LTLIBRARIES =
pkglib_LTLIBRARIES =

SUBDIRS =

AM_LDFLAGS = $(TS_PLUGIN_LD_FLAGS)

include authproxy/Makefile.inc
include background_fetch/Makefile.inc
include cachekey/Makefile.inc
include conf_remap/Makefile.inc
include esi/Makefile.inc
include generator/Makefile.inc
include compress/Makefile.inc
include header_rewrite/Makefile.inc
include healthchecks/Makefile.inc
include libloader/Makefile.inc
include regex_remap/Makefile.inc
include regex_revalidate/Makefile.inc
include s3_auth/Makefile.inc
include stats_over_http/Makefile.inc
include tcpinfo/Makefile.inc
include xdebug/Makefile.inc
include test_cppapi/Makefile.inc

if BUILD_EXPERIMENTAL_PLUGINS

include experimental/acme/Makefile.inc
include experimental/balancer/Makefile.inc
include experimental/buffer_upload/Makefile.inc
include experimental/cache_promote/Makefile.inc
include experimental/cache_range_requests/Makefile.inc
include experimental/collapsed_connection/Makefile.inc
include experimental/collapsed_forwarding/Makefile.inc
include experimental/custom_redirect/Makefile.inc
include experimental/epic/Makefile.inc
include experimental/escalate/Makefile.inc
include experimental/fq_pacing/Makefile.inc
include experimental/geoip_acl/Makefile.inc
include experimental/header_freq/Makefile.inc
include experimental/header_normalize/Makefile.inc
include experimental/hipes/Makefile.inc
include experimental/hook-trace/Makefile.inc
include experimental/inliner/Makefile.inc
include experimental/memcache/Makefile.inc
include experimental/metalink/Makefile.inc
include experimental/money_trace/Makefile.inc
include experimental/mp4/Makefile.inc
include experimental/multiplexer/Makefile.inc
include experimental/remap_purge/Makefile.inc
include experimental/server_push_preload/Makefile.inc
include experimental/ssl_cert_loader/Makefile.inc
include experimental/sslheaders/Makefile.inc
include experimental/stale_while_revalidate/Makefile.inc
include experimental/stream_editor/Makefile.inc
include experimental/system_stats/Makefile.inc
include experimental/tls_bridge/Makefile.inc
include experimental/ts_lua/Makefile.inc
include experimental/url_sig/Makefile.inc

if BUILD_URI_SIGNING_PLUGIN
include experimental/uri_signing/Makefile.inc
endif

if BUILD_MEMCACHED_REMAP_PLUGIN
include experimental/memcached_remap/Makefile.inc
endif

if BUILD_REMAP_STATS_PLUGIN
include experimental/remap_stats/Makefile.inc
endif

if HAS_KYOTOCABINET
include experimental/cache_key_genid/Makefile.inc
endif

if BUILD_WEBP_TRANSFORM_PLUGIN
include experimental/webp_transform/Makefile.inc
endif

if HAS_MYSQL
include experimental/mysql_remap/Makefile.inc
endif

endif

TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=suppression.txt

TESTS = $(check_PROGRAMS)

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
