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

.. ts:stat:: global proxy.process.net.calls_to_readfromnet_afterpoll integer
   :type: counter
   :ungathered:

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

.. ts:stat:: global proxy.process.net.calls_to_writetonet_afterpoll integer
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
.. ts:stat:: global proxy.process.net.net_handler_run integer
   :type: counter

.. ts:stat:: global proxy.process.net.read_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.net.write_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.tcp.total_accepts integer
   :type: counter

   The total number of times a TCP connection was accepted on a proxy port. This may differ from the
   total of other network connection counters. For example if a user agent connects via TLS but
   sends a malformed ``CLIENT_HELLO`` this will count as a TCP connect but not an SSL connect.
