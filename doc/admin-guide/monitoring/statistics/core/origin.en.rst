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

.. _admin-stats-core-origin:

Origin Server
*************

.. ts:stat:: global proxy.process.http.origin_server_total_request_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_total_response_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.origin_server_total_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_request_document_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_request_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_response_document_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_response_header_total_size integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_100 integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_100K integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_100M integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_10K integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_10M integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_1K integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_server_speed_bytes_per_sec_1M integer
   :type: derivative
   :units: bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.pool_lock_contention integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.migration_failure integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_no_keep_alive integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_eos integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_server_plugin_tunnel integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_transform_read integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_no_sharing integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_no_keep_alive integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_invalid_response integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_invalid_request integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_modified integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.release_misc integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.cleanup_entry integer
   :type counter
   :units bytes

.. ts:stat:: global proxy.process.http.origin_shutdown.tunnel_abort integer
   :type counter
   :units bytes

