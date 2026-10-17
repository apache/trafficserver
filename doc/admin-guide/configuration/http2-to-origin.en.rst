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

A remap rule takes effect when :file:`remap.config` is reloaded. To advertise
these protocols globally, merge this setting into :file:`records.yaml` and
restart |TS|; the global record is not reloadable:

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

The policies are reloadable. Each HTTP/2 connection reads the policy when it
is established, so a reload applies to new connections, while existing
connections keep the policy and windows they started with until they close.
Older releases copy the policies only at startup and require a restart; there,
seeing a new value in ``traffic_ctl config get`` after a reload does not verify
that HTTP/2 is using it.

Pooling and timeouts
====================

|TS| places every HTTP/2 origin session in the session pool of the network
thread that drives its connection, whatever
:ts:cv:`proxy.config.http.server_session_sharing.pool` is set to. The
session's state is owned by that thread, so it cannot be shared with other
threads. When a transaction needs an origin connection, |TS| first searches
the current thread's pool and then the pool type configured by that record.
With ``global`` pooling, HTTP/1.1 origin connections are still shared by all
threads, while an HTTP/2 origin session serves only transactions on its own
thread. :ts:cv:`proxy.config.http.server_session_sharing.match` applies to
both protocols.

Older releases search only the configured pool type. With ``global`` pooling
they do not find HTTP/2 origin sessions in the thread pool, so they open a new
origin connection for each transaction. Use ``thread`` or ``hybrid`` pooling
with those releases.

Otherwise, enabling HTTP/2 to origin does not require a different pool type.
Keep the setting that suits the origins that continue to negotiate HTTP/1.1.
The pool type is not reloadable, so changing it requires a restart.

Because HTTP/2 origin sessions are kept per thread, reuse depends on how often
each thread sends a request to the origin. Requests that arrive together on
one client connection, such as the parallel requests of a page load, are
handled on the same thread and can share one origin connection. An origin
that receives only occasional requests can find no idle session on the
current thread and open a new connection, even when another thread holds an
idle session to it. HTTP/2 therefore improves reuse most for busy origins and
for bursts of concurrent requests. For an origin with a low request rate, a
longer HTTP/1.1 keep-alive timeout can give comparable reuse.

:ts:cv:`proxy.config.http.keep_alive_no_activity_timeout_out` controls how
long an idle HTTP/1.1 origin connection remains available for reuse after a
transaction ends. Lowering it globally during an HTTP/2 rollout reduces reuse
for every origin that still negotiates HTTP/1.1, and makes changes harder to
attribute to HTTP/2. Keep the existing value while evaluating HTTP/2, and
adjust it for specific origins with :ref:`admin-plugins-conf-remap` if needed.
An HTTP/2 origin session with no active streams is instead closed after
:ts:cv:`proxy.config.http2.no_activity_timeout_out`. A longer value keeps idle
sessions available for reuse longer, at the cost of more idle connections.
A stalled transaction on either protocol is governed by
:ts:cv:`proxy.config.http.transaction_no_activity_timeout_out`; choose it to
allow expected pauses during active work.

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

Related HTTP/2 tuning
=====================

These settings can affect HTTP/2 traffic during an origin rollout, but they
are not requirements for negotiating HTTP/2 with origins:

* :ts:cv:`proxy.config.http2.active_timeout_in` limits the lifetime of an
  incoming HTTP/2 connection even when it is active. Keep its default of
  ``0`` (disabled), or select a lifetime that accommodates long-lived active
  connections.
* :ts:cv:`proxy.config.http2.min_avg_window_update` protects against peers
  sending very small WINDOW_UPDATE increments. It applies to origins as well as
  clients. When a peer's recent increments average below the threshold, |TS|
  responds with ``ENHANCE_YOUR_CALM``, resetting the stream or closing the
  connection, so an origin that sends small increments can lose requests. A
  value such as ``2.0`` relaxes the default threshold substantially. Only lower
  it after confirming that legitimate small updates trigger this protection,
  and first review window sizing. This local threshold does not prevent an
  origin from rejecting excessive SETTINGS frames.

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

Compare a canary with a baseline using similar traffic and normalize errors by
request volume. Compare connection reuse per origin rather than only the
overall share of HTTP/2 transactions; a high HTTP/2 share can come from a few
busy origins while less busy origins see little change. Separate client aborts,
connection failures, proxy-generated errors, and origin HTTP errors. Break down
changes by origin, method, status, and negotiated protocol; an aggregate error
category can also include unrelated routing failures. Allow traffic and
connection reuse to stabilize before drawing conclusions.

If failures increase, inspect GOAWAY error codes and stream resets with
bounded diagnostic logging. A single HTTP/2 connection failure can affect
several in-flight requests.
