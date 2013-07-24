.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under
   the Apache License, Version 2.0 (the "License"); you may not use
   this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an "AS
   IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
   express or implied.  See the License for the specific language
   governing permissions and limitations under the License.

conf_remap Plugin
=================

The `conf_remap` plugin allows you to override configuration
directives dependent on actual remapping rules. The plugin is built
and installed as part of the normal Apache Traffic Server installation
process.

If you want to achieve this behaviour now, configure a remap rule
like this::

    map http://cdn.example.com/ http://some-server.example.com @plugin=conf_remap.so @pparam=/etc/trafficserver/cdn.conf

where `cdn.conf` would look like :file:`records.config`. For example::

    CONFIG proxy.config.url_remap.pristine_host_hdr INT 1

Doing this, you will override your global default configuration on
a per mapping rule. For now, those options may be overridden through
the `conf_remap` plugin:

|
|
| proxy.config.url_remap.pristine_host_hdr
| proxy.config.http.chunking_enabled
| proxy.config.http.negative_caching_enabled
| proxy.config.http.negative_caching_lifetime
| proxy.config.http.cache.when_to_revalidate
| proxy.config.http.keep_alive_enabled_in
| proxy.config.http.keep_alive_enabled_out
| proxy.config.http.keep_alive_post_out
| proxy.config.http.share_server_sessions
| proxy.config.net.sock_recv_buffer_size_out
| proxy.config.net.sock_send_buffer_size_out
| proxy.config.net.sock_option_flag_out
| proxy.config.http.forward.proxy_auth_to_parent
| proxy.config.http.anonymize_remove_from
| proxy.config.http.anonymize_remove_referer
| proxy.config.http.anonymize_remove_user_agent
| proxy.config.http.anonymize_remove_cookie
| proxy.config.http.anonymize_remove_client_ip
| proxy.config.http.anonymize_insert_client_ip
| proxy.config.http.response_server_enabled
| proxy.config.http.insert_squid_x_forwarded_for
| proxy.config.http.server_tcp_init_cwnd
| proxy.config.http.send_http11_requests
| proxy.config.http.cache.http
| proxy.config.http.cache.cluster_cache_local
| proxy.config.http.cache.ignore_client_no_cache
| proxy.config.http.cache.ignore_client_cc_max_age
| proxy.config.http.cache.ims_on_client_no_cache
| proxy.config.http.cache.ignore_server_no_cache
| proxy.config.http.cache.cache_responses_to_cookies
| proxy.config.http.cache.ignore_authentication
| proxy.config.http.cache.cache_urls_that_look_dynamic
| proxy.config.http.cache.required_headers
| proxy.config.http.insert_request_via_str
| proxy.config.http.insert_response_via_str
| proxy.config.http.cache.heuristic_min_lifetime
| proxy.config.http.cache.heuristic_max_lifetime
| proxy.config.http.cache.guaranteed_min_lifetime
| proxy.config.http.cache.guaranteed_max_lifetime
| proxy.config.http.cache.max_stale_age
| proxy.config.http.keep_alive_no_activity_timeout_in
| proxy.config.http.keep_alive_no_activity_timeout_out
| proxy.config.http.transaction_no_activity_timeout_in
| proxy.config.http.transaction_no_activity_timeout_out
| proxy.config.http.transaction_active_timeout_out
| proxy.config.http.origin_max_connections
| proxy.config.http.connect_attempts_max_retries
| proxy.config.http.connect_attempts_max_retries_dead_server
| proxy.config.http.connect_attempts_rr_retries
| proxy.config.http.connect_attempts_timeout
| proxy.config.http.post_connect_attempts_timeout
| proxy.config.http.down_server.cache_time
| proxy.config.http.down_server.abort_threshold
| proxy.config.http.cache.fuzz.time
| proxy.config.http.cache.fuzz.min_time
| proxy.config.http.doc_in_cache_skip_dns
| proxy.config.http.background_fill_active_timeout
| proxy.config.http.response_server_str
| proxy.config.http.cache.heuristic_lm_factor
| proxy.config.http.cache.fuzz.probability
| proxy.config.http.background_fill_completed_threshold
| proxy.config.net.sock_packet_mark_out
| proxy.config.net.sock_packet_tos_out
| proxy.config.http.insert_age_in_response
| proxy.config.http.chunking.size
| proxy.config.http.flow_control.enabled
| proxy.config.http.flow_control.low_water
| proxy.config.http.flow_control.high_water
