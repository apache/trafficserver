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

.. _admin-stats-core-network-io:

Network I/O
***********

.. ts:stat:: global proxy.process.net.accepts_currently_open integer
   :type: counter

.. ts:stat:: global proxy.process.net.calls_to_readfromnet integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.calls_to_read integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.calls_to_read_nodata integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.calls_to_write integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.calls_to_write_nodata integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.calls_to_writetonet integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.net.connections_currently_open integer
   :type: counter

.. ts:stat:: global proxy.process.net.connections_throttled_in integer
   :type: counter

.. ts:stat:: global proxy.process.net.connections_throttled_out integer
   :type: counter

.. ts:stat:: global proxy.process.net.max.requests_throttled_in integer
   :type: counter

.. ts:stat:: global proxy.process.net.default_inactivity_timeout_applied integer
   The total number of connections that had no transaction or connection level timer running on them and
   had to fallback to the catch-all 'default_inactivity_timeout'
   :type: counter
.. ts:stat:: global proxy.process.net.default_inactivity_timeout_count integer
   The total number of connections that were cleaned up due to 'default_inactivity_timeout'
   :type: counter

.. ts:stat:: global proxy.process.net.dynamic_keep_alive_timeout_in_count integer
.. ts:stat:: global proxy.process.net.dynamic_keep_alive_timeout_in_total integer
.. ts:stat:: global proxy.process.net.inactivity_cop_lock_acquire_failure integer
.. ts:stat:: global proxy.process.net.inactivity_cop_visited integer
   The total number of connections the inactivity cop has examined. The cop
   finds expired connections through a per-thread timer wheel, so this grows
   with the number of connections whose deadline came due, not with the number
   of open connections. A value that tracks total open connections instead
   means something is re-arming every tick.
   :type: counter
.. ts:stat:: global proxy.process.net.inactivity_cop_budget_exhausted integer
   The number of inactivity cop runs that hit their per-run work limit. Such a
   run reschedules itself a millisecond out rather than waiting for the next
   periodic check, so an occasional nonzero value just means a burst of
   connections came due together. A value that climbs steadily means the thread is retiring
   timeouts more slowly than they come due, and timeouts there are firing late.
   :type: counter
.. ts:stat:: global proxy.process.net.inactivity_cop_passes integer
   The number of inactivity cop passes made, across all threads. Chiefly a
   denominator: divide the two metrics below by this for per-pass averages.
   :type: counter
.. ts:stat:: global proxy.process.net.inactivity_cop_fired integer
   The total number of connections the inactivity cop has timed out. Compare
   against :ts:stat:`proxy.process.net.inactivity_cop_visited`: the cop examines
   a connection whose deadline came due but re-arms it without firing when the
   deadline has since moved out, so ``visited`` is always at least ``fired``, and
   a large gap means deadlines are being extended after being scheduled.
   :type: counter
.. ts:stat:: global proxy.process.net.inactivity_cop_pass_time_us integer
   Accumulated microseconds spent in inactivity cop passes, covering the timer
   wheel walk and the connection closes it dispatches inline. Divided by
   :ts:stat:`proxy.process.net.inactivity_cop_fired` this gives the average cost
   of timing out one connection, which is what determines how long a full-budget
   pass can occupy an event thread.
   :type: counter
.. ts:stat:: global proxy.process.net.inactivity_cop_pass_max_us integer
   The longest single inactivity cop pass observed, in microseconds. Because the
   cop runs on an event thread, a pass of *N* microseconds delays that thread's
   poll loop by *N* microseconds, so this is the latency a burst of simultaneous
   timeouts imposes on unrelated connections. This is a best-effort high-water
   mark updated without a lock, so simultaneous passes on different threads can
   occasionally lose a sample; it may understate the true worst case but never
   reports a value no pass took.
   :type: gauge
.. ts:stat:: global proxy.process.net.net_handler_run integer
   :type: counter

.. ts:stat:: global proxy.process.net.read_bytes integer
   :type: counter
   :units: bytes

   Application-layer bytes read from client and origin connections.  For TLS
   connections this is the decrypted payload, symmetric with ``write_bytes``; it
   does not include TLS handshake or record-layer framing.

.. ts:stat:: global proxy.process.net.read_bytes_count integer
   :type: counter

   The number of read operations that contributed to ``read_bytes``.  For TLS
   connections this is one per decrypted-read pass, not per socket read.

.. ts:stat:: global proxy.process.net.write_bytes integer
   :type: counter
   :units: bytes

   Application-layer bytes written to client and origin connections.  For TLS
   connections this is the plaintext payload; it does not include TLS handshake
   or record-layer framing.

.. ts:stat:: global proxy.process.net.write_bytes_count integer
   :type: counter

   The number of write operations that contributed to ``write_bytes``.

.. ts:stat:: global proxy.process.tcp.total_accepts integer
   :type: counter

   The total number of times a TCP connection was accepted on a proxy port. This may differ from the
   total of other network connection counters. For example if a user agent connects via TLS but
   sends a malformed ``CLIENT_HELLO`` this will count as a TCP connect but not an SSL connect.
