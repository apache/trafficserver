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

.. _admin-stats-core-dns:

DNS
***

.. ts:stat:: global proxy.process.dns.fail_avg_time integer
   :type: derivative
   :units: milliseconds
   :ungathered:

   The average time per DNS lookup, in milliseconds, which ultimately failed.

.. ts:stat:: global proxy.process.dns.in_flight integer
   :type: gauge
   :ungathered:

   The number of DNS lookups currently in progress.

.. ts:stat:: global proxy.process.dns.tcp_retries integer
   :type: gauge
   :ungathered:

   The number of DNS query over TCP in TCP_RETRY connection mode.

.. ts:stat:: global proxy.process.dns.tcp_reset integer
   :type: gauge
   :ungathered:

   The number of resetting TCP connection in TCP_RETRY connection mode.

.. ts:stat:: global proxy.process.dns.lookup_avg_time integer
   :type: derivative
   :units: milliseconds
   :ungathered:

   The average time spent performing DNS lookups per host.

.. ts:stat:: global proxy.process.dns.lookup_failures integer
   :type: counter
   :ungathered:

   The total number of DNS lookups which have failed since statistics collection
   began.

.. ts:stat:: global proxy.process.dns.lookup_successes integer
   :type: counter
   :ungathered:

   The total number of DNS lookups which have succeeded since statistics
   collection began.

.. ts:stat:: global proxy.process.dns.max_retries_exceeded integer
   :type: counter
   :ungathered:

   The number of DNS lookups which have been failed due to the maximum number
   of retries being exceeded.

.. ts:stat:: global proxy.process.dns.retries integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.dns.success_avg_time integer
   :type: derivative
   :units: milliseconds
   :ungathered:

   The average time per DNS lookup, in milliseconds, which have succeeded.

.. ts:stat:: global proxy.process.dns.total_dns_lookups integer
   :type: counter
   :ungathered:

   The total number of DNS lookups which have been performed since statistics
   collection began.

