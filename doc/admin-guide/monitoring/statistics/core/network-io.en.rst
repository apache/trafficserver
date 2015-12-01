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

.. ts:stat:: global proxy.process.net.default_inactivity_timeout_applied integer
.. ts:stat:: global proxy.process.net.dynamic_keep_alive_timeout_in_count integer
.. ts:stat:: global proxy.process.net.dynamic_keep_alive_timeout_in_total integer
.. ts:stat:: global proxy.process.net.inactivity_cop_lock_acquire_failure integer
.. ts:stat:: global proxy.process.net.net_handler_run integer
   :type: counter

.. ts:stat:: global proxy.process.net.read_bytes integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.net.write_bytes integer
   :type: counter
   :unit: bytes

