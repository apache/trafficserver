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

.. _admin-stats-core-hostdb:

HostDB
******

The HostDB component of |TS| manages origin server name resolution and host
health checks. The statistics documented here can help to ensure that your |TS|
instances are not spending an unreasonable amount of timing resolving your
origin servers' hostnames prior to object revalidation or retrieval.

.. ts:stat:: global proxy.node.hostdb.hit_ratio_avg_10s float
   :type: derivative
   :unit: ratio

   Represents the ratio of origin server name resolutions which were satisfied
   by the HostDB lookup cache over the last 10 seconds.

.. ts:stat:: global proxy.node.hostdb.hit_ratio float
   :type: derivative
   :unit: ratio

   Represents the ratio of origin server name resolutions which were satisfied
   by the HostDB lookup cache since statistics collection began.

.. ts:stat:: global proxy.node.hostdb.hit_ratio_int_pct integer
   :type: derivative
   :unit: ratio

   The value of :ts:stat:`proxy.node.hostdb.hit_ratio` converted to an integer
   percent.

.. ts:stat:: global proxy.node.hostdb.total_hits_avg_10s float
   :type: derivative

   Represents the number of origin server name resolutions which were satisfied
   by the HostDB lookup cache over the last 10 seconds.

.. ts:stat:: global proxy.node.hostdb.total_hits integer
   :type: counter

   Represents the total number of origin server name resolutions which were
   satisfied by entries in the HostDB lookup cache, since statistics collection
   began.

.. ts:stat:: global proxy.node.hostdb.total_lookups_avg_10s float
   :type: derivative

   Represents the number of origin server name resolutions which were performed
   over the last 10 seconds, regardless of whether they were satisfied by the
   HostDB lookup cache.

.. ts:stat:: global proxy.node.hostdb.total_lookups integer
   :type: counter

   Represents the total number of origin server name resolutions which have been
   performed, since statistics collection began, regardless of whether they were
   satisfied by the HostDB lookup cache or required DNS lookups.

.. ts:stat:: global proxy.process.hostdb.bytes integer
   :type: counter
   :unit: bytes

   Represents the number of bytes allocated to the HostDB lookup cache.

.. ts:stat:: global proxy.process.hostdb.re_dns_on_reload integer
   :type: counter

.. ts:stat:: global proxy.process.hostdb.total_entries integer
   :type: counter

   Represents the total number of host entries currently in the HostDB lookup
   cache.

.. ts:stat:: global proxy.process.hostdb.total_hits integer
   :type: counter

   Represents the total number of origin server name resolutions which were
   satisfied by entries in the HostDB lookup cache, since statistics collection
   began.

.. ts:stat:: global proxy.process.hostdb.total_lookups integer
   :type: counter

   Represents the total number of origin server name resolutions which have been
   performed, since statistics collection began, regardless of whether they were
   satisfied by the HostDB lookup cache or required DNS lookups.

.. ts:stat:: global proxy.process.hostdb.ttl_expires integer
   :type: gauge
   :unit: seconds

.. ts:stat:: global proxy.process.hostdb.ttl float
   :type: gauge
   :unit: seconds

