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

.. _admin-stats-core-hierarchical:

Hierarchical Cache
******************

.. ts:stat:: global proxy.process.http.parent_proxy_total_request_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.current_parent_proxy_connections integer
   :type: counter

.. ts:stat:: global proxy.process.http.parent_proxy_request_total_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.parent_proxy_response_total_bytes integer
   :type: counter
   :units: bytes

.. ts:stat:: global proxy.process.http.parent_proxy_transaction_time integer
   :type: counter
   :units: seconds

.. ts:stat:: global proxy.process.http.total_parent_proxy_connections integer
   :type: counter
