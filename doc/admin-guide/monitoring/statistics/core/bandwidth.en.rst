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

.. _admin-stats-core-bandwidth:

Bandwidth and Transfer
**********************

.. ts:stat:: global proxy.node.bandwidth_hit_ratio_avg_10s float
   :type: derivative
   :unit: ratio

   The difference of :ts:stat:`proxy.node.user_agent_total_bytes_avg_10s` and
   :ts:stat:`proxy.node.origin_server_total_bytes_avg_10s`, divided by
   :ts:stat:`proxy.node.user_agent_total_bytes_avg_10s`.

   Represents the ratio of bytes served to user agents which were satisfied by
   cache hits, over the previous 10 seconds,

.. ts:stat:: global proxy.node.bandwidth_hit_ratio_avg_10s_int_pct integer
   :type: derivative
   :unit: percent

   The percentage value of :ts:stat:`proxy.node.bandwidth_hit_ratio_avg_10s`
   converted to an integer.

.. ts:stat:: global proxy.node.bandwidth_hit_ratio_int_pct integer
   :type: derivative
   :unit: percent

   The percentage vaue of :ts:stat:`proxy.node.bandwidth_hit_ratio` converted
   to an integer.

.. ts:stat:: global proxy.node.bandwidth_hit_ratio float
   :type: derivative
   :unit: ratio

   The difference of :ts:stat:`proxy.node.user_agent_total_bytes` and
   :ts:stat:`proxy.node.origin_server_total_bytes`, divided by
   :ts:stat:`proxy.node.user_agent_total_bytes`.

   Represents the ratio of bytes served to user agents which were satisfied by
   cache hits, since statistics collection began.

.. ts:stat:: global proxy.node.client_throughput_out float
   :type: gauge
   :unit: mbits

   The value of :ts:stat:`proxy.node.http.throughput` represented in megabits.

.. ts:stat:: global proxy.node.client_throughput_out_kbit integer
   :type: gauge
   :unit: kbits

   The value of :ts:stat:`proxy.node.http.throughput` represented in kilobits.

.. ts:stat:: global proxy.node.http.throughput integer
   :type: gauge
   :unit: bytes

   The throughput of responses to user agents over the previous 10 seconds, in
   bytes.

.. ts:stat:: global proxy.process.http.throttled_proxy_only integer
.. ts:stat:: global proxy.process.http.user_agent_request_document_total_size integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_request_header_total_size integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.http.user_agent_response_document_total_size integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.http.user_agent_response_header_total_size integer
   :type: counter
   :unit: bytes

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100 integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100K integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100M integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_10K integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_10M integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_1K integer
   :type: derivative
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_1M integer
   :type: derivative
   :unit: bytes
   :ungathered:

