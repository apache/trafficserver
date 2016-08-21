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

SUBDIRS = \
 acme \
 balancer \
 buffer_upload \
 cache_promote \
 cache_range_requests \
 cachekey \
 collapsed_connection \
 collapsed_forwarding \
 custom_redirect \
 epic \
 escalate \
 geoip_acl \
 header_normalize \
 hipes \
 inliner \
 memcache \
 memcached_remap \
 metalink \
 mp4 \
 multiplexer \
 remap_stats \
 remap_purge \
 ssl_cert_loader \
 sslheaders \
 stale_while_revalidate \
 stream_editor \
 ts_lua \
 url_sig

if BUILD_WEBP_TRANSFORM_PLUGIN
 SUBDIRS += webp_transform
endif

if HAS_MYSQL
  SUBDIRS += mysql_remap
endif

if HAS_KYOTOCABINET
  SUBDIRS += cache_key_genid
endif

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
