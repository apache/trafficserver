.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. default-domain:: c

.. _ts-overridable-config:

=======================
TSHttpOverridableConfig
=======================

Synopsis
========
`#include <ts/ts.h>`

.. type:: TSOverridableConfigKey

.. function:: TSReturnCode TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt value)
.. function:: TSReturnCode TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt* value)
.. function:: TSReturnCode TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat value)
.. function:: TSReturnCode TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat* value)
.. function:: TSReturnCode TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey key, const char* value, int length)
.. function:: TSReturnCode TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey key, const char** value, int* length)
.. function:: TSReturnCode TSHttpTxnConfigFind(const char* name, int length, TSOverridableConfigKey* key, TSRecordDataType* type)

Description
===========

Some of the values that are set in :file:`records.config` can be changed for a specific transaction. It is important to
note that these functions change the configuration values stored for the transation, which is not quite the same as
changing the actual operating values of the transaction. The critical effect is the value must be changed before it is
used by the transaction - after that, changes will not have any effect.

All of the ``...Get`` functions store the internal value in the storage indicated by the :arg:`value` argument. For strings :arg:`length*` will receive the length of the string.

The values are identified by the enumeration :type:`TSOverridableConfigKey`. String values can be used indirectly by
first passing them to :func:`TSHttpTxnConfigFind` which, if the string matches an overridable value, return the key and data
type.

Configurations
==============

The following configurations (from ``records.config``) are overridable: ::

    proxy.config.url_remap.pristine_host_hdr
    proxy.config.http.chunking_enabled
    proxy.config.http.negative_caching_enabled
    proxy.config.http.negative_caching_lifetime
    proxy.config.http.cache.when_to_revalidate
    proxy.config.http.keep_alive_enabled_in
    proxy.config.http.keep_alive_enabled_out
    proxy.config.http.keep_alive_post_out
    proxy.config.http.share_server_sessions
    proxy.config.net.sock_recv_buffer_size_out
    proxy.config.net.sock_send_buffer_size_out
    proxy.config.net.sock_option_flag_out
    proxy.config.http.forward.proxy_auth_to_parent
    proxy.config.http.anonymize_remove_from
    proxy.config.http.anonymize_remove_referer
    proxy.config.http.anonymize_remove_user_agent
    proxy.config.http.anonymize_remove_cookie
    proxy.config.http.anonymize_remove_client_ip
    proxy.config.http.anonymize_insert_client_ip
    proxy.config.http.response_server_enabled
    proxy.config.http.insert_squid_x_forwarded_for
    proxy.config.http.server_tcp_init_cwnd
    proxy.config.http.send_http11_requests
    proxy.config.http.cache.http
    proxy.config.http.cache.cluster_cache_local
    proxy.config.http.cache.ignore_client_no_cache
    proxy.config.http.cache.ignore_client_cc_max_age
    proxy.config.http.cache.ims_on_client_no_cache
    proxy.config.http.cache.ignore_server_no_cache
    proxy.config.http.cache.cache_responses_to_cookies
    proxy.config.http.cache.ignore_authentication
    proxy.config.http.cache.cache_urls_that_look_dynamic
    proxy.config.http.cache.required_headers
    proxy.config.http.insert_request_via_str
    proxy.config.http.insert_response_via_str
    proxy.config.http.cache.heuristic_min_lifetime
    proxy.config.http.cache.heuristic_max_lifetime
    proxy.config.http.cache.guaranteed_min_lifetime
    proxy.config.http.cache.guaranteed_max_lifetime
    proxy.config.http.cache.max_stale_age
    proxy.config.http.keep_alive_no_activity_timeout_in
    proxy.config.http.keep_alive_no_activity_timeout_out
    proxy.config.http.transaction_no_activity_timeout_in
    proxy.config.http.transaction_no_activity_timeout_out
    proxy.config.http.transaction_active_timeout_out
    proxy.config.http.origin_max_connections
    proxy.config.http.connect_attempts_max_retries
    proxy.config.http.connect_attempts_max_retries_dead_server
    proxy.config.http.connect_attempts_rr_retries
    proxy.config.http.connect_attempts_timeout
    proxy.config.http.post_connect_attempts_timeout
    proxy.config.http.down_server.cache_time
    proxy.config.http.down_server.abort_threshold
    proxy.config.http.cache.fuzz.time
    proxy.config.http.cache.fuzz.min_time
    proxy.config.http.doc_in_cache_skip_dns
    proxy.config.http.background_fill_active_timeout
    proxy.config.http.response_server_str
    proxy.config.http.cache.heuristic_lm_factor
    proxy.config.http.cache.fuzz.probability
    proxy.config.http.background_fill_completed_threshold
    proxy.config.net.sock_packet_mark_out
    proxy.config.net.sock_packet_tos_out
    proxy.config.http.insert_age_in_response
    proxy.config.http.chunking.size
    proxy.config.http.flow_control.enabled
    proxy.config.http.flow_control.low_water
    proxy.config.http.flow_control.high_water
    proxy.config.http.cache.range.lookup
    proxy.config.http.normalize_ae_gzip
    proxy.config.http.default_buffer_size
    proxy.config.http.default_buffer_water_mark
    proxy.config.http.request_header_max_size
    proxy.config.http.response_header_max_size
    proxy.config.http.negative_revalidating_enabled
    proxy.config.http.negative_revalidating_lifetime
    proxy.config.http.accept_encoding_filter_enabled


Examples
========

Enable :ref:`transaction buffer control <transaction-buffering-control>` with a high water mark of 262144 and a low water mark of 65536. ::

   int callback(TSCont contp, TSEvent event, void* data)
   {
      TSHttpTxn txnp = static_cast<TSHttpTxn>(data);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED, 1);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK, 262144);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_LOWER_WATER_MARK, 65536);
      return 0;
   }

See also
========
:manpage:`TSAPI(3ts)`
