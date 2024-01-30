#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

add_subdirectory(access_control)
add_subdirectory(block_errors)
add_subdirectory(cache_fill)
add_subdirectory(cert_reporting_tool)
add_subdirectory(cookie_remap)
add_subdirectory(custom_redirect)
add_subdirectory(fq_pacing)
add_subdirectory(geoip_acl)
add_subdirectory(header_freq)
add_subdirectory(hook-trace)
add_subdirectory(http_stats)
add_subdirectory(icap)
add_subdirectory(inliner)
add_subdirectory(memcache)
add_subdirectory(memory_profile)
add_subdirectory(money_trace)
add_subdirectory(mp4)
add_subdirectory(rate_limit)
add_subdirectory(redo_cache_lookup)
add_subdirectory(sslheaders)
add_subdirectory(stale_response)
add_subdirectory(stream_editor)
add_subdirectory(system_stats)
add_subdirectory(tls_bridge)
add_subdirectory(url_sig)
add_subdirectory(txn_box)

if(USE_MAGICK)
  add_subdirectory(magick)
endif()
#add_subdirectory(maxmind_acl)
#add_subdirectory(otel_tracer)
#add_subdirectory(stek_share)
#add_subdirectory(uri_signing)
#add_subdirectory(wasm)
