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

.. default-domain:: cpp

.. _ts-overridable-config:

TSHttpOverridableConfig
***************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt value)
.. function:: TSReturnCode TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt* value)
.. function:: TSReturnCode TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat value)
.. function:: TSReturnCode TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat* value)
.. function:: TSReturnCode TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey key, const char* value, int length)
.. function:: TSReturnCode TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey key, const char** value, int* length)
.. function:: TSReturnCode TSHttpTxnConfigFind(const char* name, int length, TSOverridableConfigKey* key, TSRecordDataType* type)

Description
===========

Some of the values that are set in :file:`records.yaml` can be changed for a
specific transaction. It is important to note that these functions change the
configuration values stored for the transaction, which is not quite the same as
changing the actual operating values of the transaction. The critical effect is
the value must be changed before it is used by the transaction - after that,
changes will not have any effect.

All of the ``...Get`` functions store the internal value in the storage
indicated by the :arg:`value` argument. For strings :arg:`length*` will receive
the length of the string.

The values are identified by the enumeration :enum:`TSOverridableConfigKey`.
String values can be used indirectly by first passing them to
:func:`TSHttpTxnConfigFind` which, if the string matches an overridable value,
return the key and data type.

Configurations
==============

Testing :enumerator:`TS_CONFIG_BODY_FACTORY_TEMPLATE_BASE`.

The following configurations (from ``records.yaml``) are overridable:

======================================================================  ====================================================================
TSOverridableConfigKey Value                                              Configuration Value
======================================================================  ====================================================================
:enumerator:`TS_CONFIG_BODY_FACTORY_TEMPLATE_BASE`                      :ts:cv:`proxy.config.body_factory.template_base`
:enumerator:`TS_CONFIG_HTTP_ALLOW_HALF_OPEN`                            :ts:cv:`proxy.config.http.allow_half_open`
:enumerator:`TS_CONFIG_HTTP_ALLOW_MULTI_RANGE`                          :ts:cv:`proxy.config.http.allow_multi_range`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_INSERT_CLIENT_IP`                 :ts:cv:`proxy.config.http.insert_client_ip`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_REMOVE_CLIENT_IP`                 :ts:cv:`proxy.config.http.anonymize_remove_client_ip`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_REMOVE_COOKIE`                    :ts:cv:`proxy.config.http.anonymize_remove_cookie`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_REMOVE_FROM`                      :ts:cv:`proxy.config.http.anonymize_remove_from`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_REMOVE_REFERER`                   :ts:cv:`proxy.config.http.anonymize_remove_referer`
:enumerator:`TS_CONFIG_HTTP_ANONYMIZE_REMOVE_USER_AGENT`                :ts:cv:`proxy.config.http.anonymize_remove_user_agent`
:enumerator:`TS_CONFIG_HTTP_ATTACH_SERVER_SESSION_TO_CLIENT`            :ts:cv:`proxy.config.http.attach_server_session_to_client`
:enumerator:`TS_CONFIG_HTTP_MAX_PROXY_CYCLES`                           :ts:cv:`proxy.config.http.max_proxy_cycles`
:enumerator:`TS_CONFIG_HTTP_AUTH_SERVER_SESSION_PRIVATE`                :ts:cv:`proxy.config.http.auth_server_session_private`
:enumerator:`TS_CONFIG_HTTP_BACKGROUND_FILL_ACTIVE_TIMEOUT`             :ts:cv:`proxy.config.http.background_fill_active_timeout`
:enumerator:`TS_CONFIG_HTTP_BACKGROUND_FILL_COMPLETED_THRESHOLD`        :ts:cv:`proxy.config.http.background_fill_completed_threshold`
:enumerator:`TS_CONFIG_HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES`           :ts:cv:`proxy.config.http.cache.cache_responses_to_cookies`
:enumerator:`TS_CONFIG_HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC`         :ts:cv:`proxy.config.http.cache.cache_urls_that_look_dynamic`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_QUERY`                         :ts:cv:`proxy.config.http.cache.ignore_query`
:enumerator:`TS_CONFIG_HTTP_CACHE_GENERATION`                           :ts:cv:`proxy.config.http.cache.generation`
:enumerator:`TS_CONFIG_HTTP_CACHE_GUARANTEED_MAX_LIFETIME`              :ts:cv:`proxy.config.http.cache.guaranteed_max_lifetime`
:enumerator:`TS_CONFIG_HTTP_CACHE_GUARANTEED_MIN_LIFETIME`              :ts:cv:`proxy.config.http.cache.guaranteed_min_lifetime`
:enumerator:`TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR`                  :ts:cv:`proxy.config.http.cache.heuristic_lm_factor`
:enumerator:`TS_CONFIG_HTTP_CACHE_HEURISTIC_MAX_LIFETIME`               :ts:cv:`proxy.config.http.cache.heuristic_max_lifetime`
:enumerator:`TS_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME`               :ts:cv:`proxy.config.http.cache.heuristic_min_lifetime`
:enumerator:`TS_CONFIG_HTTP_CACHE_HTTP`                                 :ts:cv:`proxy.config.http.cache.http`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_CHARSET_MISMATCH`       :ts:cv:`proxy.config.http.cache.ignore_accept_charset_mismatch`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_ENCODING_MISMATCH`      :ts:cv:`proxy.config.http.cache.ignore_accept_encoding_mismatch`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_LANGUAGE_MISMATCH`      :ts:cv:`proxy.config.http.cache.ignore_accept_language_mismatch`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_ACCEPT_MISMATCH`               :ts:cv:`proxy.config.http.cache.ignore_accept_mismatch`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_AUTHENTICATION`                :ts:cv:`proxy.config.http.cache.ignore_authentication`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE`             :ts:cv:`proxy.config.http.cache.ignore_client_cc_max_age`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_NO_CACHE`               :ts:cv:`proxy.config.http.cache.ignore_client_no_cache`
:enumerator:`TS_CONFIG_HTTP_CACHE_IGNORE_SERVER_NO_CACHE`               :ts:cv:`proxy.config.http.cache.ignore_server_no_cache`
:enumerator:`TS_CONFIG_HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE`               :ts:cv:`proxy.config.http.cache.ims_on_client_no_cache`
:enumerator:`TS_CONFIG_HTTP_CACHE_MAX_OPEN_READ_RETRIES`                :ts:cv:`proxy.config.http.cache.max_open_read_retries`
:enumerator:`TS_CONFIG_HTTP_CACHE_MAX_OPEN_WRITE_RETRIES`               :ts:cv:`proxy.config.http.cache.max_open_write_retries`
:enumerator:`TS_CONFIG_HTTP_CACHE_MAX_STALE_AGE`                        :ts:cv:`proxy.config.http.cache.max_stale_age`
:enumerator:`TS_CONFIG_HTTP_CACHE_OPEN_READ_RETRY_TIME`                 :ts:cv:`proxy.config.http.cache.open_read_retry_time`
:enumerator:`TS_CONFIG_HTTP_CACHE_OPEN_WRITE_FAIL_ACTION`               :ts:cv:`proxy.config.http.cache.open_write_fail_action`
:enumerator:`TS_CONFIG_HTTP_CACHE_RANGE_LOOKUP`                         :ts:cv:`proxy.config.http.cache.range.lookup`
:enumerator:`TS_CONFIG_HTTP_CACHE_RANGE_WRITE`                          :ts:cv:`proxy.config.http.cache.range.write`
:enumerator:`TS_CONFIG_HTTP_CACHE_REQUIRED_HEADERS`                     :ts:cv:`proxy.config.http.cache.required_headers`
:enumerator:`TS_CONFIG_HTTP_CACHE_WHEN_TO_REVALIDATE`                   :ts:cv:`proxy.config.http.cache.when_to_revalidate`
:enumerator:`TS_CONFIG_HTTP_CHUNKING_ENABLED`                           :ts:cv:`proxy.config.http.chunking_enabled`
:enumerator:`TS_CONFIG_HTTP_CHUNKING_SIZE`                              :ts:cv:`proxy.config.http.chunking.size`
:enumerator:`TS_CONFIG_HTTP_DROP_CHUNKED_TRAILERS`                      :ts:cv:`proxy.config.http.drop_chunked_trailers`
:enumerator:`TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER`   :ts:cv:`proxy.config.http.connect_attempts_max_retries_down_server`
:enumerator:`TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES`               :ts:cv:`proxy.config.http.connect_attempts_max_retries`
:enumerator:`TS_CONFIG_HTTP_CONNECT_ATTEMPTS_RR_RETRIES`                :ts:cv:`proxy.config.http.connect_attempts_rr_retries`
:enumerator:`TS_CONFIG_HTTP_CONNECT_ATTEMPTS_TIMEOUT`                   :ts:cv:`proxy.config.http.connect_attempts_timeout`
:enumerator:`TS_CONFIG_HTTP_DEFAULT_BUFFER_SIZE`                        :ts:cv:`proxy.config.http.default_buffer_size`
:enumerator:`TS_CONFIG_HTTP_DEFAULT_BUFFER_WATER_MARK`                  :ts:cv:`proxy.config.http.default_buffer_water_mark`
:enumerator:`TS_CONFIG_HTTP_DOC_IN_CACHE_SKIP_DNS`                      :ts:cv:`proxy.config.http.doc_in_cache_skip_dns`
:enumerator:`TS_CONFIG_HTTP_DOWN_SERVER_CACHE_TIME`                     :ts:cv:`proxy.config.http.down_server.cache_time`
:enumerator:`TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED`                       :ts:cv:`proxy.config.http.flow_control.enabled`
:enumerator:`TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK`               :ts:cv:`proxy.config.http.flow_control.high_water`
:enumerator:`TS_CONFIG_HTTP_FLOW_CONTROL_LOW_WATER_MARK`                :ts:cv:`proxy.config.http.flow_control.low_water`
:enumerator:`TS_CONFIG_HTTP_FORWARD_CONNECT_METHOD`                     :ts:cv:`proxy.config.http.forward_connect_method`
:enumerator:`TS_CONFIG_HTTP_FORWARD_PROXY_AUTH_TO_PARENT`               :ts:cv:`proxy.config.http.forward.proxy_auth_to_parent`
:enumerator:`TS_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER`                   :ts:cv:`proxy.config.http.global_user_agent_header`
:enumerator:`TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE`                     :ts:cv:`proxy.config.http.insert_age_in_response`
:enumerator:`TS_CONFIG_HTTP_INSERT_FORWARDED`                           :ts:cv:`proxy.config.http.insert_forwarded`
:enumerator:`TS_CONFIG_HTTP_INSERT_REQUEST_VIA_STR`                     :ts:cv:`proxy.config.http.insert_request_via_str`
:enumerator:`TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR`                    :ts:cv:`proxy.config.http.insert_response_via_str`
:enumerator:`TS_CONFIG_HTTP_INSERT_SQUID_X_FORWARDED_FOR`               :ts:cv:`proxy.config.http.insert_squid_x_forwarded_for`
:enumerator:`TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_IN`                      :ts:cv:`proxy.config.http.keep_alive_enabled_in`
:enumerator:`TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_OUT`                     :ts:cv:`proxy.config.http.keep_alive_enabled_out`
:enumerator:`TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN`          :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_in`
:enumerator:`TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT`         :ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_out`
:enumerator:`TS_CONFIG_HTTP_KEEP_ALIVE_POST_OUT`                        :ts:cv:`proxy.config.http.keep_alive_post_out`
:enumerator:`TS_CONFIG_HTTP_NEGATIVE_CACHING_ENABLED`                   :ts:cv:`proxy.config.http.negative_caching_enabled`
:enumerator:`TS_CONFIG_HTTP_NEGATIVE_CACHING_LIFETIME`                  :ts:cv:`proxy.config.http.negative_caching_lifetime`
:enumerator:`TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_ENABLED`              :ts:cv:`proxy.config.http.negative_revalidating_enabled`
:enumerator:`TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_LIFETIME`             :ts:cv:`proxy.config.http.negative_revalidating_lifetime`
:enumerator:`TS_CONFIG_HTTP_NO_DNS_JUST_FORWARD_TO_PARENT`              :ts:cv:`proxy.config.http.no_dns_just_forward_to_parent`
:enumerator:`TS_CONFIG_HTTP_NORMALIZE_AE`                               :ts:cv:`proxy.config.http.normalize_ae`
:enumerator:`TS_CONFIG_HTTP_NUMBER_OF_REDIRECTIONS`                     :ts:cv:`proxy.config.http.number_of_redirections`
:enumerator:`TS_CONFIG_HTTP_PARENT_PROXY_FAIL_THRESHOLD`                :ts:cv:`proxy.config.http.parent_proxy.fail_threshold`
:enumerator:`TS_CONFIG_HTTP_PARENT_PROXY_RETRY_TIME`                    :ts:cv:`proxy.config.http.parent_proxy.retry_time`
:enumerator:`TS_CONFIG_HTTP_PARENT_PROXY_TOTAL_CONNECT_ATTEMPTS`        :ts:cv:`proxy.config.http.parent_proxy.total_connect_attempts`
:enumerator:`TS_CONFIG_HTTP_PER_PARENT_CONNECT_ATTEMPTS`                :ts:cv:`proxy.config.http.parent_proxy.per_parent_connect_attempts`
:enumerator:`TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MATCH`                :ts:cv:`proxy.config.http.per_server.connection.match`
:enumerator:`TS_CONFIG_HTTP_PER_SERVER_CONNECTION_MAX`                  :ts:cv:`proxy.config.http.per_server.connection.max`
:enumerator:`TS_CONFIG_HTTP_POST_CHECK_CONTENT_LENGTH_ENABLED`          :ts:cv:`proxy.config.http.post.check.content_length.enabled`
:enumerator:`TS_CONFIG_HTTP_REDIRECT_USE_ORIG_CACHE_KEY`                :ts:cv:`proxy.config.http.redirect_use_orig_cache_key`
:enumerator:`TS_CONFIG_HTTP_REQUEST_BUFFER_ENABLED`                     :ts:cv:`proxy.config.http.request_buffer_enabled`
:enumerator:`TS_CONFIG_HTTP_REQUEST_HEADER_MAX_SIZE`                    :ts:cv:`proxy.config.http.request_header_max_size`
:enumerator:`TS_CONFIG_HTTP_RESPONSE_HEADER_MAX_SIZE`                   :ts:cv:`proxy.config.http.response_header_max_size`
:enumerator:`TS_CONFIG_HTTP_RESPONSE_SERVER_ENABLED`                    :ts:cv:`proxy.config.http.response_server_enabled`
:enumerator:`TS_CONFIG_HTTP_RESPONSE_SERVER_STR`                        :ts:cv:`proxy.config.http.response_server_str`
:enumerator:`TS_CONFIG_HTTP_SEND_HTTP11_REQUESTS`                       :ts:cv:`proxy.config.http.send_http11_requests`
:enumerator:`TS_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH`               :ts:cv:`proxy.config.http.server_session_sharing.match`
:enumerator:`TS_CONFIG_HTTP_SLOW_LOG_THRESHOLD`                         :ts:cv:`proxy.config.http.slow.log.threshold`
:enumerator:`TS_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_IN`              :ts:cv:`proxy.config.http.transaction_active_timeout_in`
:enumerator:`TS_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT`             :ts:cv:`proxy.config.http.transaction_active_timeout_out`
:enumerator:`TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN`         :ts:cv:`proxy.config.http.transaction_no_activity_timeout_in`
:enumerator:`TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT`        :ts:cv:`proxy.config.http.transaction_no_activity_timeout_out`
:enumerator:`TS_CONFIG_HTTP_UNCACHEABLE_REQUESTS_BYPASS_PARENT`         :ts:cv:`proxy.config.http.uncacheable_requests_bypass_parent`
:enumerator:`TS_CONFIG_NET_SOCK_OPTION_FLAG_OUT`                        :ts:cv:`proxy.config.net.sock_option_flag_out`
:enumerator:`TS_CONFIG_NET_SOCK_PACKET_MARK_OUT`                        :ts:cv:`proxy.config.net.sock_packet_mark_out`
:enumerator:`TS_CONFIG_NET_SOCK_PACKET_TOS_OUT`                         :ts:cv:`proxy.config.net.sock_packet_tos_out`
:enumerator:`TS_CONFIG_NET_SOCK_RECV_BUFFER_SIZE_OUT`                   :ts:cv:`proxy.config.net.sock_recv_buffer_size_out`
:enumerator:`TS_CONFIG_NET_DEFAULT_INACTIVITY_TIMEOUT`                  :ts:cv:`proxy.config.net.default_inactivity_timeout`
:enumerator:`TS_CONFIG_NET_SOCK_SEND_BUFFER_SIZE_OUT`                   :ts:cv:`proxy.config.net.sock_send_buffer_size_out`
:enumerator:`TS_CONFIG_PARENT_FAILURES_UPDATE_HOSTDB`                   :ts:cv:`proxy.config.http.parent_proxy.mark_down_hostdb`
:enumerator:`TS_CONFIG_SRV_ENABLED`                                     :ts:cv:`proxy.config.srv_enabled`
:enumerator:`TS_CONFIG_SSL_CLIENT_CERT_FILENAME`                        :ts:cv:`proxy.config.ssl.client.cert.filename`
:enumerator:`TS_CONFIG_SSL_CERT_FILEPATH`                               :ts:cv:`proxy.config.ssl.client.cert.path`
:enumerator:`TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_PROPERTIES`             :ts:cv:`proxy.config.ssl.client.verify.server.properties`
:enumerator:`TS_CONFIG_SSL_CLIENT_VERIFY_SERVER_POLICY`                 :ts:cv:`proxy.config.ssl.client.verify.server.policy`
:enumerator:`TS_CONFIG_SSL_CLIENT_SNI_POLICY`                           :ts:cv:`proxy.config.ssl.client.sni_policy`
:enumerator:`TS_CONFIG_SSL_HSTS_INCLUDE_SUBDOMAINS`                     :ts:cv:`proxy.config.ssl.hsts_include_subdomains`
:enumerator:`TS_CONFIG_SSL_HSTS_MAX_AGE`                                :ts:cv:`proxy.config.ssl.hsts_max_age`
:enumerator:`TS_CONFIG_URL_REMAP_PRISTINE_HOST_HDR`                     :ts:cv:`proxy.config.url_remap.pristine_host_hdr`
:enumerator:`TS_CONFIG_WEBSOCKET_ACTIVE_TIMEOUT`                        :ts:cv:`proxy.config.websocket.active_timeout`
:enumerator:`TS_CONFIG_WEBSOCKET_NO_ACTIVITY_TIMEOUT`                   :ts:cv:`proxy.config.websocket.no_activity_timeout`
:enumerator:`TS_CONFIG_SSL_CLIENT_CERT_FILENAME`                        :ts:cv:`proxy.config.ssl.client.cert.filename`
:enumerator:`TS_CONFIG_SSL_CLIENT_PRIVATE_KEY_FILENAME`                 :ts:cv:`proxy.config.ssl.client.private_key.filename`
:enumerator:`TS_CONFIG_SSL_CLIENT_CA_CERT_FILENAME`                     :ts:cv:`proxy.config.ssl.client.CA.cert.filename`
:enumerator:`TS_CONFIG_HTTP_HOST_RESOLUTION_PREFERENCE`                 :ts:cv:`proxy.config.hostdb.ip_resolve`
:enumerator:`TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_INDEX`                  :ts:cv:`proxy.config.plugin.vc.default_buffer_index`
:enumerator:`TS_CONFIG_PLUGIN_VC_DEFAULT_BUFFER_WATER_MARK`             :ts:cv:`proxy.config.plugin.vc.default_buffer_water_mark`
:enumerator:`TS_CONFIG_NET_SOCK_NOTSENT_LOWAT`                          :ts:cv:`proxy.config.net.sock_notsent_lowat`
:enumerator:`TS_CONFIG_BODY_FACTORY_RESPONSE_SUPPRESSION_MODE`          :ts:cv:`proxy.config.body_factory.response_suppression_mode`
:enumerator:`TS_CONFIG_HTTP_CACHE_POST_METHOD`                          :ts:cv:`proxy.config.http.cache.post_method`
======================================================================  ====================================================================

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
