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
 authproxy \
 background_fetch \
 balancer \
 buffer_upload \
 cachekey \
 cache_range_requests \
 cache_promote \
 collapsed_connection \
 custom_redirect \
 epic \
 escalate \
 esi \
 generator \
 geoip_acl \
 header_normalize \
 hipes \
 inliner \
 metalink \
 multiplexer \
 memcache \
 memcached_remap \
 regex_revalidate \
 remap_stats \
 s3_auth \
 ssl_cert_loader \
 sslheaders \
 stale_while_revalidate \
 url_sig \
 xdebug \
 mp4 \
 stream_editor

if HAS_MYSQL
  SUBDIRS += mysql_remap
endif

if BUILD_LUAJIT
  SUBDIRS += ts_lua
endif
