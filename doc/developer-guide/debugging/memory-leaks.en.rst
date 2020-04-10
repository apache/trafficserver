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

.. _developer-memory-leaks:

Memory Leaks
************

Memory leaks in a plugin can be detected using e.g. an MRTG graph
related to memory - you can use memory dump information. Enable
``mem dump`` in :file:`records.config` as follows:

::

      CONFIG proxy.config.dump_mem_info_frequency INT <value>

This causes Traffic Server to dump memory information to ``traffic.out``
at ``<value>`` (intervals are in seconds). A zero value means that it is
disabled.

::

      CONFIG proxy.config.res_track_memory INT <value>

   When enabled makes Traffic Server track memory usage (allocations and releases). This
   information is dumped  to ``traffic.out`` when the user sends a SIGUSR1 signal or
   periodically when :ts:cv:`proxy.config.dump_mem_info_frequency` is enabled.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Memory tracking Disabled
   ``1`` Tracks IO Buffer Memory allocations and releases
   ``2`` Tracks IO Buffer Memory and OpenSSL Memory allocations and releases
   ===== ======================================================================
