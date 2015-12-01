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

.. _admin-stats-core-http-method:

HTTP Request Method
*******************

The statistics documented in this section provide counters for the number of
incoming requests to the |TS| instance, broken down by the HTTP method of the
request.

.. ts:stat:: global proxy.process.http.delete_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`DELETE` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.extension_method_requests integer
   :type: counter

.. ts:stat:: global proxy.process.http.get_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`GET` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.head_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`HEAD` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.invalid_client_requests integer
   :type: counter

   Represents the total number of requests received by the |TS| instance, since
   statistics collection began, which did not include a valid HTTP method.

.. ts:stat:: global proxy.process.http.options_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`OPTIONS` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.outgoing_requests integer
   :type: counter

.. ts:stat:: global proxy.process.http.post_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`POST` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.purge_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`PURGE` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.push_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`PUSH` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.put_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`PUT` requests received by
   the |TS| instance since statistics collection began.

.. ts:stat:: global proxy.process.http.trace_requests integer
   :type: counter

   Represents the total number of HTTP :literal:`TRACE` requests received by
   the |TS| instance since statistics collection began.

