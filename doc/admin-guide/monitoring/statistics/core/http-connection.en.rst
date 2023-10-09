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

.. include:: ../../../../common.defs

.. _admin-stats-core-http-connection:

HTTP Connection
***************

.. ts:stat:: global proxy.process.current_server_connections integer
   :type: gauge

.. ts:stat:: global proxy.process.http.user_agent_total_request_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.user_agent_total_response_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.broken_server_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.completed_requests integer
   :type: counter

.. ts:stat:: global proxy.process.http.connect_requests integer
   :type: counter

.. ts:stat:: global proxy.process.http.current_active_client_connections integer
   :type: gauge

   Represents the current number of HTTP/1.0 and HTTP/1.1 connections
   from client to the |TS|.

.. ts:stat:: global proxy.process.http.current_cache_connections integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.http.current_client_connections integer
   :type: gauge

.. ts:stat:: global proxy.process.http.current_client_transactions integer
   :type: gauge

.. ts:stat:: global proxy.process.http.current_server_connections integer
   :type: gauge

.. ts:stat:: global proxy.process.http.current_server_transactions integer
   :type: gauge

.. ts:stat:: global proxy.process.http.err_client_abort_count_stat integer
   :type: counter

.. ts:stat:: global proxy.process.http.err_client_abort_origin_server_bytes_stat integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_abort_user_agent_bytes_stat integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_read_error_count_stat integer
   :type: counter

.. ts:stat:: global proxy.process.http.err_client_read_error_origin_server_bytes_stat integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_read_error_user_agent_bytes_stat integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_connect_fail_count_stat integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.err_connect_fail_origin_server_bytes_stat integer
   :type: counter
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.err_connect_fail_user_agent_bytes_stat integer
   :type: counter
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.http_misc_origin_server_bytes_stat integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.incoming_requests integer
   :type: counter

.. ts:stat:: global proxy.process.http.incoming_responses integer
   :type: counter

.. ts:stat:: global proxy.process.https.incoming_requests integer
   :type: counter

.. ts:stat:: global proxy.process.https.total_client_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_client_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_client_connections_ipv4 integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_client_connections_ipv6 integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_incoming_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_server_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.origin_connections_throttled_out integer
   :type: counter

This tracks the number of origin connections denied due to being over the :ts:cv:`proxy.config.http.origin_max_connections` limit.


HTTP/2
------


.. ts:stat:: global proxy.process.http2.total_client_connections integer
   :type: counter

   Represents the total number of HTTP/2 connections from client to the |TS|.

.. ts:stat:: global proxy.process.http2.current_client_connections integer
   :type: gauge

   Represents the current number of HTTP/2 connections from client to the |TS|.

.. ts:stat:: global proxy.process.http2.current_active_client_connections integer
   :type: gauge

   Represents the current number of HTTP/2 active connections from client to the |TS|.

.. ts:stat:: global proxy.process.http2.connection_errors integer
   :type: counter

   Represents the total number of HTTP/2 connections errors.

.. ts:stat:: global proxy.process.http2.session_die_default integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   ``VC_EVENT_NONE`` event.

.. ts:stat:: global proxy.process.http2.session_die_active integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   ``VC_EVENT_ACTIVE_TIMEOUT`` event.

.. ts:stat:: global proxy.process.http2.session_die_inactive integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   ``VC_EVENT_INACTIVITY_TIMEOUT`` event.

.. ts:stat:: global proxy.process.http2.session_die_eos integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   ``VC_EVENT_EOS`` event.

.. ts:stat:: global proxy.process.http2.session_die_error integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   ``VC_EVENT_ERROR`` event.

.. ts:stat:: global proxy.process.http2.session_die_other integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with
   unknown event.

.. ts:stat:: global proxy.process.http2.session_die_high_error_rate integer
   :type: counter

   Represents the total number of closed HTTP/2 connections with high
   error rate which is configured by :ts:cv:`proxy.config.http2.stream_error_rate_threshold`.

.. ts:stat:: global proxy.process.http2.max_settings_per_frame_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of settings per frame limit which is configured by
   :ts:cv:`proxy.config.http2.max_settings_per_frame`.

.. ts:stat:: global proxy.process.http2.max_settings_per_minute_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of settings per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_settings_per_minute`.

.. ts:stat:: global proxy.process.http2.max_settings_frames_per_minute_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of settings frames per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_settings_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.max_ping_frames_per_minute_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of ping frames per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_ping_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.max_priority_frames_per_minute_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of priority frames per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_priority_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.max_rst_stream_frames_per_minute_exceeded integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for exceeding the
   maximum allowed number of rst_stream frames per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_rst_stream_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.insufficient_avg_window_update integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for not reaching the
   minimum average window increment limit which is configured by
   :ts:cv:`proxy.config.http2.min_avg_window_update`.
