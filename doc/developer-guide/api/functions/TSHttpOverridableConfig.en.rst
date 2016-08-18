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

.. include:: ../../../common.defs

.. default-domain:: c

.. _ts-overridable-config:

TSHttpOverridableConfig
***********************

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

Some of the values that are set in :file:`records.config` can be changed for a
specific transaction. It is important to note that these functions change the
configuration values stored for the transation, which is not quite the same as
changing the actual operating values of the transaction. The critical effect is
the value must be changed before it is used by the transaction - after that,
changes will not have any effect.

All of the ``...Get`` functions store the internal value in the storage
indicated by the :arg:`value` argument. For strings :arg:`length*` will receive
the length of the string.

The values are identified by the enumeration :type:`TSOverridableConfigKey`.
String values can be used indirectly by first passing them to
:func:`TSHttpTxnConfigFind` which, if the string matches an overridable value,
return the key and data type.

Configurations
==============

The following configurations (from ``records.config``) are overridable.

|   :ts:cv:`proxy.config.url_remap.pristine_host_hdr`
|   :ts:cv:`proxy.config.http.chunking_enabled`
|   :ts:cv:`proxy.config.http.negative_caching_enabled`
|   :ts:cv:`proxy.config.http.negative_caching_lifetime`
|   :ts:cv:`proxy.config.http.cache.when_to_revalidate`
|   :ts:cv:`proxy.config.http.keep_alive_enabled_in`
|   :ts:cv:`proxy.config.http.keep_alive_enabled_out`
|   :ts:cv:`proxy.config.http.keep_alive_post_out`
|   :ts:cv:`proxy.config.net.sock_recv_buffer_size_out`
|   :ts:cv:`proxy.config.net.sock_send_buffer_size_out`
|   :ts:cv:`proxy.config.net.sock_option_flag_out`
|   :ts:cv:`proxy.config.http.forward.proxy_auth_to_parent`
|   :ts:cv:`proxy.config.http.anonymize_remove_from`
|   :ts:cv:`proxy.config.http.anonymize_remove_referer`
|   :ts:cv:`proxy.config.http.anonymize_remove_user_agent`
|   :ts:cv:`proxy.config.http.anonymize_remove_cookie`
|   :ts:cv:`proxy.config.http.anonymize_remove_client_ip`
|   :ts:cv:`proxy.config.http.anonymize_insert_client_ip`
|   :ts:cv:`proxy.config.http.response_server_enabled`
|   :ts:cv:`proxy.config.http.insert_squid_x_forwarded_for`
|   :ts:cv:`proxy.config.http.server_tcp_init_cwnd`
|   :ts:cv:`proxy.config.http.send_http11_requests`
|   :ts:cv:`proxy.config.http.cache.http`
|   :ts:cv:`proxy.config.http.cache.cluster_cache_local`
|   :ts:cv:`proxy.config.http.cache.ignore_client_no_cache`
|   :ts:cv:`proxy.config.http.cache.ignore_client_cc_max_age`
|   :ts:cv:`proxy.config.http.cache.ims_on_client_no_cache`
|   :ts:cv:`proxy.config.http.cache.ignore_server_no_cache`
|   :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`
|   :ts:cv:`proxy.config.http.cache.ignore_authentication`
|   :ts:cv:`proxy.config.http.cache.cache_urls_that_look_dynamic`
|   :ts:cv:`proxy.config.http.cache.required_headers`
|   :ts:cv:`proxy.config.http.insert_request_via_str`
|   :ts:cv:`proxy.config.http.insert_response_via_str`
|   :ts:cv:`proxy.config.http.cache.heuristic_min_lifetime`
|   :ts:cv:`proxy.config.http.cache.heuristic_max_lifetime`
|   :ts:cv:`proxy.config.http.cache.guaranteed_min_lifetime`
|   :ts:cv:`proxy.config.http.cache.guaranteed_max_lifetime`
|   :ts:cv:`proxy.config.http.cache.max_stale_age`
|   :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_in`
|   :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_out`
|   :ts:cv:`proxy.config.http.transaction_no_activity_timeout_in`
|   :ts:cv:`proxy.config.http.transaction_no_activity_timeout_out`
|   :ts:cv:`proxy.config.http.transaction_active_timeout_out`
|   :ts:cv:`proxy.config.websocket.no_activity_timeout`
|   :ts:cv:`proxy.config.websocket.active_timeout`
|   :ts:cv:`proxy.config.http.origin_max_connections`
|   :ts:cv:`proxy.config.http.connect_attempts_max_retries`
|   :ts:cv:`proxy.config.http.connect_attempts_max_retries_dead_server`
|   :ts:cv:`proxy.config.http.connect_attempts_rr_retries`
|   :ts:cv:`proxy.config.http.connect_attempts_timeout`
|   :ts:cv:`proxy.config.http.post_connect_attempts_timeout`
|   :ts:cv:`proxy.config.http.down_server.cache_time`
|   :ts:cv:`proxy.config.http.down_server.abort_threshold`
|   :ts:cv:`proxy.config.http.cache.fuzz.time`
|   :ts:cv:`proxy.config.http.cache.fuzz.min_time`
|   :ts:cv:`proxy.config.http.doc_in_cache_skip_dns`
|   :ts:cv:`proxy.config.http.background_fill_active_timeout`
|   :ts:cv:`proxy.config.http.response_server_str`
|   :ts:cv:`proxy.config.http.cache.heuristic_lm_factor`
|   :ts:cv:`proxy.config.http.cache.fuzz.probability`
|   :ts:cv:`proxy.config.http.background_fill_completed_threshold`
|   :ts:cv:`proxy.config.net.sock_packet_mark_out`
|   :ts:cv:`proxy.config.net.sock_packet_tos_out`
|   :ts:cv:`proxy.config.http.insert_age_in_response`
|   :ts:cv:`proxy.config.http.chunking.size`
|   :ts:cv:`proxy.config.http.flow_control.enabled`
|   :ts:cv:`proxy.config.http.flow_control.low_water`
|   :ts:cv:`proxy.config.http.flow_control.high_water`
|   :ts:cv:`proxy.config.http.cache.range.lookup`
|   :ts:cv:`proxy.config.http.normalize_ae_gzip`
|   :ts:cv:`proxy.config.http.default_buffer_size`
|   :ts:cv:`proxy.config.http.default_buffer_water_mark`
|   :ts:cv:`proxy.config.http.request_header_max_size`
|   :ts:cv:`proxy.config.http.response_header_max_size`
|   :ts:cv:`proxy.config.http.negative_revalidating_enabled`
|   :ts:cv:`proxy.config.http.negative_revalidating_lifetime`
|   :ts:cv:`proxy.config.http.accept_encoding_filter_enabled`
|   :ts:cv:`proxy.config.http.cache.range.write`
|   :ts:cv:`proxy.config.http.global_user_agent_header`
|   :ts:cv:`proxy.config.http.slow.log.threshold`
|   :ts:cv:`proxy.config.http.cache.generation`
|   :ts:cv:`proxy.config.body_factory.template_base`
|   :ts:cv:`proxy.config.http.cache.open_write_fail_action`
|   :ts:cv:`proxy.config.http.redirection_enabled`
|   :ts:cv:`proxy.config.http.number_of_redirections`
|   :ts:cv:`proxy.config.http.cache.max_open_write_retries`
|   :ts:cv:`proxy.config.http.redirect_use_orig_cache_key`

Examples
========

Enable :ref:`transaction buffer control <transaction-buffering-control>` with a
high water mark of :literal:`262144` and a low water mark of :literal:`65536`. ::

   int callback(TSCont contp, TSEvent event, void* data)
   {
      TSHttpTxn txnp = static_cast<TSHttpTxn>(data);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED, 1);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK, 262144);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_LOWER_WATER_MARK, 65536);
      return 0;
   }

See Also
========

:manpage:`TSAPI(3ts)`
