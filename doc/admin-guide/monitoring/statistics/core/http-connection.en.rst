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

   Represents the current number of HTTP/1.0 and HTTP/1.1 transactions from client to the |TS|.

.. ts:stat:: global proxy.process.http.current_server_connections integer
   :type: gauge

.. ts:stat:: global proxy.process.http.current_server_transactions integer
   :type: gauge

.. ts:stat:: global proxy.process.http.err_client_abort_count integer
   :type: counter

.. ts:stat:: global proxy.process.http.err_client_abort_origin_server_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_abort_user_agent_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_read_error_count integer
   :type: counter

.. ts:stat:: global proxy.process.http.err_client_read_error_origin_server_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_client_read_error_user_agent_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.err_connect_fail_count integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.err_connect_fail_origin_server_bytes integer
   :type: counter
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.err_connect_fail_user_agent_bytes integer
   :type: counter
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.http_misc_origin_server_bytes integer
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

.. ts:stat:: global proxy.process.http.total_client_connections_uds integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_incoming_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_parent_marked_down_count integer
   :type: counter

   Tracks the number of times a parent was marked down.

.. ts:stat:: global proxy.process.http.total_parent_marked_down_timeout integer
   :type: counter

   Tracks the number of times a parent was marked down due to a inactivity timeout.

.. ts:stat:: global proxy.process.http.total_server_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.origin_connections_throttled_out integer
   :type: counter

   This tracks the number of origin connections denied due to being over the :ts:cv:`proxy.config.http.per_server.connection.max` limit.

.. ts:stat:: global proxy.process.http.pooled_server_connections integer
   :type: counter

   This metric tracks the number of server connections currently in the server session sharing pools. The server session sharing is
   controlled by settings :ts:cv:`proxy.config.http.server_session_sharing.pool` and :ts:cv:`proxy.config.http.server_session_sharing.match`.

.. ts:stat:: global proxy.process.http.down_server.no_requests integer
   :type: counter

   Tracks the number of client requests that did not have a request sent to the origin server because the origin server was marked down.

.. ts:stat:: global proxy.process.http.http_proxy_loop_detected integer
   :type: counter

   Counts the number of times a proxy loop was detected

.. ts:stat:: global proxy.process.http.http_proxy_mh_loop_detected integer
   :type: counter

   Counts the number of times a multi-hop proxy loop was detected

.. ts:stat:: global proxy.process.http_parent_count integer
   :type: counter

   Counts the number of times current parent or next parent was detected

.. ts:stat:: global proxy.process.tunnel.total_client_connections_blind_tcp integer
   :type: counter

   Total number of non-TLS TCP connections for tunnels where the far end is the client
   initiated with an HTTP request (such as a CONNECT or WebSocket request).

.. ts:stat:: global proxy.process.tunnel.current_client_connections_blind_tcp integer
   :type: counter

   Current number of non-TLS TCP connections for tunnels where the far end is the client
   initiated with an HTTP request (such as a CONNECT or WebSocket request).

.. ts:stat:: global proxy.process.tunnel.total_server_connections_blind_tcp integer
   :type: counter

   Total number of TCP connections for tunnels where the far end is the server,
   except for those counted by ``proxy.process.tunnel.total_server_connections_tls``

.. ts:stat:: global proxy.process.tunnel.current_server_connections_blind_tcp integer
   :type: counter

   Current number of TCP connections for tunnels where the far end is the server,
   except for those counted by ``proxy.process.tunnel.current_server_connections_tls``

.. _per-server-connection-metrics:

Per Server Connection Metrics
-----------------------------

Unlike the metrics above these do not have fixed names. They are created dynamically, one set per
upstream server group as defined by :ts:cv:`proxy.config.http.per_server.connection.match`, and,
when the match type is ``both``, one aggregate set per hostname. Whether they are collected at all is
controlled by :ts:cv:`proxy.config.http.per_server.connection.metric_enabled`, and which of them are
published by :ts:cv:`proxy.config.http.per_server.connection.metric_aggregate`. An optional
:ts:cv:`proxy.config.http.per_server.connection.metric_prefix` can be inserted into the names.

Per group names are ``proxy.process.http.per_server.<counter>.<group>``, where ``<group>`` depends on
the match type: an IP address, an ``address:port`` pair, a hostname, or, for ``both``,
``<hostname>.<address:port>``. Per hostname names are
``proxy.process.http.per_server.<counter>.<hostname>``. Aggregates exist only for match type
``both``, because that is the only match type whose group key carries the hostname. An ``ip`` or
``port`` group is keyed on the address alone and is shared by every hostname that resolves to it, so
there is no single hostname to aggregate it under. For match type ``host`` the group name is already
the bare hostname, so an aggregate would carry the same name as the single group it summarises.

For a group, ``<counter>`` is one of:

current_connection
   Gauge. The number of connections currently open to the group.

total_connection
   Counter. The total number of connections ever opened to the group. Never decreases.

blocked_connection
   Counter. The total number of connection attempts to the group blocked by
   :ts:cv:`proxy.config.http.per_server.connection.max`. Never decreases.

For a hostname aggregate, ``<counter>`` is one of those three, each summed across the groups of that
hostname which have aggregation enabled, plus:

current_connection_max
   Gauge. The largest ``current_connection`` value among the groups of that hostname at the moment
   of sampling, so the maximum rather than the sum of the groups' current counts. This is useful
   because :ts:cv:`proxy.config.http.per_server.connection.max` is enforced per group rather than
   per hostname, so the busiest group is what determines whether connections are about to be
   blocked. Like ``current_connection`` it rises and falls with traffic and is not a high-water
   mark. There is no per group ``current_connection_max``; it exists only as a hostname aggregate.

Because :ts:cv:`proxy.config.http.per_server.connection.metric_aggregate` is overridable, a group
joins its hostname's aggregate only if the mapping that first opened that upstream had aggregation
enabled. Mappings that disagree for one hostname therefore produce an aggregate over part of it: the
sums cover a subset of the groups and ``current_connection_max`` takes its maximum over that same
subset, with nothing in the metric to indicate it. Keeping the setting uniform across the mappings
for a hostname avoids this.

Because :ts:cv:`proxy.config.http.per_server.connection.match` is also overridable, one hostname can
use match type ``host`` on one mapping and ``both`` on another. The ``host`` group and the hostname
aggregate are then published under the same name and merged into a single metric that carries both,
so a hostname should use one match type throughout.

Every published per server metric is recomputed periodically, currently every 5 seconds, rather than
on every connection event, so a reader sees a value up to that interval old. This is true of the
hostname aggregates and of the published per group metrics alike: those are
mirrored from the internal ones by the same periodic mechanism, not written as connections open and
close. It applies to ``current_connection_max`` too, which reports the maximum across groups as of
the last sample rather than a running peak. To obtain the peak over a longer window, compute a
maximum over time from this gauge in the monitoring system.

At :ts:cv:`metric_aggregate <proxy.config.http.per_server.connection.metric_aggregate>` value ``2``
the per group metrics still exist internally, since the aggregates are computed from them, but are not
published. They can be listed with ``traffic_ctl metric match per_server --include-hidden``, which
reads them directly and so is not subject to the sampling delay above. That visibility is intended
for debugging and is not a stable interface: the existence, granularity and naming of the per group
metrics may change independently of the published aggregates.

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

.. ts:stat:: global proxy.process.http2.total_server_connections integer
   :type: counter

   Represents the total number of HTTP/2 connections from |TS| to the origin.

.. ts:stat:: global proxy.process.http2.current_server_connections integer
   :type: gauge

   Represents the current number of HTTP/2 connections from |TS| to the origin.

.. ts:stat:: global proxy.process.http2.current_active_server_connections integer
   :type: gauge

   Represents the current number of HTTP/2 active connections from |TS| to the origin.

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

   Represents the total number of HTTP/2 connections closed for exceeding the
   maximum allowed number of ``RST_STREAM`` frames per minute limit which is configured by
   :ts:cv:`proxy.config.http2.max_rst_stream_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.max_continuation_frames_per_minute_exceeded integer
   :type: counter

   Represents the total number of HTTP/2 connections closed for exceeding the
   maximum allowed number of ``CONTINUATION`` frames per minute limit which is
   configured by :ts:cv:`proxy.config.http2.max_continuation_frames_per_minute`.

.. ts:stat:: global proxy.process.http2.insufficient_avg_window_update integer
   :type: counter

   Represents the total number of closed HTTP/2 connections for not reaching the
   minimum average window increment limit which is configured by
   :ts:cv:`proxy.config.http2.min_avg_window_update`.

.. ts:stat:: global proxy.process.http2.max_concurrent_streams_exceeded_in integer
   :type: counter

   Represents the number of times an inbound HTTP/2 stream was not created for
   reaching the maximum number of concurrent streams per inbound connection
   configured by :ts:cv:`proxy.config.http2.max_concurrent_streams_in`.

.. ts:stat:: global proxy.process.http2.max_concurrent_streams_exceeded_out integer
   :type: counter

   Represents the number of times an outbound HTTP/2 stream was not created for
   reaching the maximum number of concurrent streams per outbound connection
   the client can initiate as specified by the server.
