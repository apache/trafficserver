.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

TSOverridableConfigKey
**********************

Synopsis
========

`#include <ts/apidefs.h>`

.. c:type:: TSOverridableConfigKey

Enum typedef.

Enumeration Members
===================

.. c:member:: TSOverridableConfigKey TS_CONFIG_NULL

.. c:member:: TSOverridableConfigKey TS_CONFIG_URL_REMAP_PRISTINE_HOST_HDR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CHUNKING_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_NEGATIVE_CACHING_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_NEGATIVE_CACHING_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_WHEN_TO_REVALIDATE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_IN

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_KEEP_ALIVE_ENABLED_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_KEEP_ALIVE_POST_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_SHARE_SERVER_SESSIONS, // DEPRECATED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_SERVER_SESSION_SHARING_POOL

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_SERVER_SESSION_SHARING_MATCH

.. c:member:: TSOverridableConfigKey TS_CONFIG_NET_SOCK_RECV_BUFFER_SIZE_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_NET_SOCK_SEND_BUFFER_SIZE_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_NET_SOCK_OPTION_FLAG_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_FORWARD_PROXY_AUTH_TO_PARENT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_REMOVE_FROM

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_REMOVE_REFERER

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_REMOVE_USER_AGENT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_REMOVE_COOKIE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_REMOVE_CLIENT_IP

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ANONYMIZE_INSERT_CLIENT_IP

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_RESPONSE_SERVER_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_INSERT_SQUID_X_FORWARDED_FOR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_SERVER_TCP_INIT_CWND

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_SEND_HTTP11_REQUESTS

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_HTTP

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_CLUSTER_CACHE_LOCAL

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_NO_CACHE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_IGNORE_CLIENT_CC_MAX_AGE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_IMS_ON_CLIENT_NO_CACHE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_IGNORE_SERVER_NO_CACHE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_CACHE_RESPONSES_TO_COOKIES

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_IGNORE_AUTHENTICATION

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_CACHE_URLS_THAT_LOOK_DYNAMIC

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_REQUIRED_HEADERS

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_INSERT_REQUEST_VIA_STR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_HEURISTIC_MAX_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_GUARANTEED_MIN_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_GUARANTEED_MAX_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_MAX_STALE_AGE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_IN

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_KEEP_ALIVE_NO_ACTIVITY_TIMEOUT_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_TRANSACTION_ACTIVE_TIMEOUT_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ORIGIN_MAX_CONNECTIONS

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CONNECT_ATTEMPTS_RR_RETRIES

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CONNECT_ATTEMPTS_TIMEOUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_POST_CONNECT_ATTEMPTS_TIMEOUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_DOWN_SERVER_CACHE_TIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_DOWN_SERVER_ABORT_THRESHOLD

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_FUZZ_TIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_FUZZ_MIN_TIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_DOC_IN_CACHE_SKIP_DNS

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_BACKGROUND_FILL_ACTIVE_TIMEOUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_RESPONSE_SERVER_STR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_FUZZ_PROBABILITY

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_BACKGROUND_FILL_COMPLETED_THRESHOLD

.. c:member:: TSOverridableConfigKey TS_CONFIG_NET_SOCK_PACKET_MARK_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_NET_SOCK_PACKET_TOS_OUT

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CHUNKING_SIZE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_FLOW_CONTROL_LOW_WATER_MARK

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_RANGE_LOOKUP

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_NORMALIZE_AE_GZIP

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_DEFAULT_BUFFER_SIZE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_DEFAULT_BUFFER_WATER_MARK

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_REQUEST_HEADER_MAX_SIZE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_RESPONSE_HEADER_MAX_SIZE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_NEGATIVE_REVALIDATING_LIFETIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_ACCEPT_ENCODING_FILTER_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_SSL_HSTS_MAX_AGE

.. c:member:: TSOverridableConfigKey TS_CONFIG_SSL_HSTS_INCLUDE_SUBDOMAINS

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_OPEN_READ_RETRY_TIME

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_MAX_OPEN_READ_RETRIES

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_CACHE_RANGE_WRITE

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_POST_CHECK_CONTENT_LENGTH_ENABLED

.. c:member:: TSOverridableConfigKey TS_CONFIG_HTTP_GLOBAL_USER_AGENT_HEADER

.. c:member:: TSOverridableConfigKey TS_CONFIG_LAST_ENTRY

Description
===========

