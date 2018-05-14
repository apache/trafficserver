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

.. ts:stat:: global proxy.process.http.throttled_proxy_only integer
.. ts:stat:: global proxy.process.http.user_agent_request_document_total_size integer
   :type: counter
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_request_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.user_agent_response_document_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.user_agent_response_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100 integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100K integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_100M integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_10K integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_10M integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_1K integer
   :type: derivative
   :units: bytes
   :ungathered:

.. ts:stat:: global proxy.process.http.user_agent_speed_bytes_per_sec_1M integer
   :type: derivative
   :units: bytes
   :ungathered:

