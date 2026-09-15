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

.. include:: ../../common.defs

.. _http2-to-origin:

HTTP/2 to Origin
****************

|TS| can negotiate HTTP/2 with TLS origin servers independently of the
protocol used by its clients. Several requests can share one origin
connection, reducing connection setup overhead. Origins that support only
HTTP/1.1 can continue to use it.

This guide describes reverse proxy connections established by |TS|. A forward
proxy's CONNECT tunnel is different: the client negotiates TLS and HTTP inside
the tunnel, so configuring |TS|'s origin ALPN does not select that protocol.

Enable protocol negotiation
===========================

Configure |TS| to advertise HTTP/2 support to origin peers via ALPN
negotiation. Set :ts:cv:`proxy.config.ssl.client.alpn_protocols` to offer
HTTP/2 with HTTP/1.1 as a fallback. To start with one remap rule, use
:ref:`admin-plugins-conf-remap` in :file:`remap.config`::

   map https://www.example.com/ https://origin.example.com/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.alpn_protocols=h2,http/1.1

To advertise these protocols globally, merge this setting into
:file:`records.yaml`:

.. code-block:: yaml

   records:
     ssl:
       client:
         alpn_protocols: h2,http/1.1

The destination must use TLS for this ALPN configuration to apply. The origin
selects a mutually supported protocol during the TLS handshake; offering
``h2`` does not force every origin connection to use HTTP/2. Existing
connections retain their negotiated protocol. Check for per-remap overrides
when a global change does not affect the expected origin.

Start with a limited set of origins and representative traffic, including
uploads, large responses, conditional requests, and long-lived connections.
Check the fixes available in your |TS| release before expanding deployment.

Flow control
============

HTTP/2 has both stream and connection receive windows. With policy ``0``,
several active streams share a connection window the size of a single stream
window. That can restrict throughput even when the individual streams have
room to receive more data.

Policy ``1`` increases the connection receive window while keeping each
stream's receive window fixed. It is a useful starting point for concurrent
traffic:

.. code-block:: yaml

   records:
     http2:
       flow_control:
         policy_in: 1
         policy_out: 1

The directions describe the connections, not the direction of request data:

* :ts:cv:`proxy.config.http2.flow_control.policy_in` controls what |TS| can
  receive from HTTP/2 clients, such as concurrent request bodies.
* :ts:cv:`proxy.config.http2.flow_control.policy_out` controls what |TS| can
  receive from HTTP/2 origins, such as concurrent response bodies. The origin's
  receive windows control how much request-body data |TS| can send to it.

Policy ``1`` sizes the connection window as the initial stream window
multiplied by the configured maximum concurrent streams for that direction.
Review :ts:cv:`proxy.config.http2.initial_window_size_out` and
:ts:cv:`proxy.config.http2.max_concurrent_streams_out` together when tuning
origin response throughput and buffering.

Policy ``2`` also enlarges the connection window, but dynamically changes the
stream windows as concurrency changes. These adjustments can generate repeated
SETTINGS frames. Some origins limit SETTINGS frequency and can terminate the
connection with GOAWAY and ``ENHANCE_YOUR_CALM``. Policy ``1`` avoids these
concurrency-driven stream-window updates. Increasing |TS|'s inbound SETTINGS
limit does not change a limit imposed by the origin.

Persist policy changes in :file:`records.yaml` and restart |TS| during a planned
maintenance window. The policies are copied into HTTP/2 state at startup;
seeing a new value in ``traffic_ctl config get`` after a reload alone does not
verify that HTTP/2 is using it.

Pooling and timeouts
====================

Review :ts:cv:`proxy.config.http.server_session_sharing.pool` together with
:ts:cv:`proxy.config.http.server_session_sharing.match`. HTTP/2 origin sessions
are associated with their network thread. ``thread`` or ``hybrid`` pooling is
a useful starting point; global HTTP/1 pooling behavior is not a guarantee
that HTTP/2 connections will be reused across threads. For example:

.. code-block:: yaml

   records:
     http:
       server_session_sharing:
         pool: hybrid
       keep_alive_no_activity_timeout_out: 30
     http2:
       no_activity_timeout_out: 30

A shorter idle timeout can reduce the number of idle connections retained
across threads and origins. A longer timeout can improve reuse for intermittent
traffic. The example uses 30 seconds; choose values appropriate for the
origins and workload. The keep-alive setting
:ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_out` concerns reuse
after a transaction ends. Separately,
:ts:cv:`proxy.config.http2.no_activity_timeout_out` controls inactivity when
an HTTP/2 origin transaction stalls. Choose that timeout to allow expected
pauses during active work; it need not match the keep-alive timeout.

:ts:cv:`proxy.config.http.per_server.connection.max` limits upstream
connections, not concurrent HTTP/2 streams. A deployment-specific value such
as 5000 is not required to enable HTTP/2. Select connection limits using the
origin's capacity, |TS|'s thread count, and observed reuse.

:ts:cv:`proxy.config.net.default_inactivity_timeout` is a connection-level
fallback used when no inactivity timeout has been set by the HTTP state
machine. Its unit is **seconds**. Long-running origin work can require
reviewing this timeout while the client connection waits for completion.
Increasing it retains resources longer, so a very large value should not be
copied into every deployment. Preserve suitable transaction timeouts; see
:ref:`admin-performance-timeouts`.

Related client-side tuning
==========================

These settings can affect an HTTP/2 client workload during an origin rollout,
but they are not requirements for negotiating HTTP/2 with origins:

* :ts:cv:`proxy.config.http2.max_concurrent_streams_in` limits concurrent
  streams from clients. Reducing it from 100 to 40 can help workloads with
  many busy streams, at the cost of less concurrency per client connection.
  It does not set the origin's limit on requests sent by |TS|.
* :ts:cv:`proxy.config.http2.active_timeout_in` limits the lifetime of an
  incoming HTTP/2 connection even when it is active. Keep its default of
  ``0`` (disabled), or select a lifetime that accommodates long-lived active
  connections.
* :ts:cv:`proxy.config.http2.min_avg_window_update` protects against peers
  sending very small WINDOW_UPDATE increments. A value such as ``2.0`` relaxes
  the default threshold substantially. Only lower it after confirming that
  legitimate small updates trigger this protection, and first review window
  sizing. This local threshold does not prevent an origin from rejecting
  excessive SETTINGS frames.

Visibility
==========

Add origin protocol logging to the transaction log to observe which origin
connections are using HTTP/2. Include the origin protocol field in the log
format in :file:`logging.yaml`::

   o_http_version="%<sqpv>"

This records the origin protocol, rather than the client protocol. Compare
requests that contacted an origin; cache hits need not establish an origin
connection. A rollout should show HTTP/2 for supporting origins and HTTP/1.1
for others, not necessarily HTTP/2 for every access-log entry.

Measure connection reuse and errors together. The gauges
``proxy.process.http.current_server_connections`` and
``proxy.process.http2.current_server_connections`` track HTTP/1 and HTTP/2
origin connections separately. A reduction in the former alone does not
prove a reduction in the total number of origin connections.

Compare a canary with a baseline using similar traffic and normalize errors
by request volume. Separate client aborts, connection failures, proxy-generated
errors, and origin HTTP errors. Break down changes by origin, method, status,
and negotiated protocol; an aggregate error category can also include
unrelated routing failures. Allow traffic and connection reuse to stabilize
before drawing conclusions.

If failures increase, inspect GOAWAY error codes and stream resets with
bounded diagnostic logging. A single HTTP/2 connection failure can affect
several in-flight requests.
