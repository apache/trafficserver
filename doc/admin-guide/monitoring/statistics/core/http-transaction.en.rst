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

.. _admin-stats-core-http-transaction:

HTTP Transaction
****************


.. ts:stat:: global proxy.process.http.avg_transactions_per_client_connection float
   :type: derivative

.. ts:stat:: global proxy.process.http.avg_transactions_per_server_connection float
   :type: derivative

.. ts:stat:: global proxy.process.http.total_transactions_time integer
   :type: counter
   :units: seconds

.. ts:stat:: global proxy.process.http.transaction_counts.errors.aborts integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.errors.connect_failed integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.errors.early_hangups integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.errors.empty_hangups integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.errors.other integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.errors.possible_aborts integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.errors.pre_accept_hangups integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.hit_fresh integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.hit_fresh.process integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.hit_revalidated integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.miss_changed integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.miss_client_no_cache integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_counts.miss_cold integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.miss_not_cacheable integer
   :type: counter

.. ts:stat:: global proxy.process.http.transaction_counts.other.unclassified integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.aborts float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.connect_failed float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.other float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.possible_aborts float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.pre_accept_hangups float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_fresh float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_fresh.process float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_revalidated float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_changed float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_client_no_cache float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_cold float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_not_cacheable float
   :type: counter
   :units: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.other.unclassified float
   :type: counter
   :units: seconds
   :ungathered:


HTTP/2
------


.. ts:stat:: global proxy.process.http2.total_client_streams integer
   :type: counter

   Represents the total number of HTTP/2 streams from client to the |TS|.

.. ts:stat:: global proxy.process.http2.current_client_streams integer
   :type: gauge

   Represents the current number of HTTP/2 streams from client to the |TS|.

.. ts:stat:: global proxy.process.http2.total_transactions_time integer
   :type: counter
   :units: seconds

   Represents the total transaction time of HTTP/2 streams from client to the |TS|.

.. ts:stat:: global proxy.process.http2.stream_errors integer
   :type: counter

   Represents the total number of HTTP/2 stream errors.
