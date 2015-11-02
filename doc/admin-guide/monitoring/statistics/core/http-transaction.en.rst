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

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.aborts float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.connect_failed float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.early_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.empty_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.other float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.possible_aborts float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.errors.pre_accept_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.hit_fresh float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.hit_revalidated float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.miss_changed float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.miss_client_no_cache float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.miss_cold float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.miss_not_cacheable float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_counts_avg_10s.other.unclassified float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.aborts float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.aborts_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.connect_failed float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.connect_failed_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.early_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.early_hangups_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.other float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.other_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.hit_fresh float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.hit_fresh_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.hit_revalidated float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.hit_revalidated_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_changed float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_changed_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_cold float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_cold_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.other.unclassified float
   :type: derivative

.. ts:stat:: global proxy.node.http.transaction_frac_avg_10s.other.unclassified_int_pct integer
   :type: derivative
   :ungathered:

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.aborts integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.connect_failed integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.early_hangups integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.other integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.hit_fresh integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.hit_revalidated integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.miss_changed integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.miss_cold integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.node.http.transaction_msec_avg_10s.other.unclassified integer
   :type: derivative
   :unit: milliseconds

.. ts:stat:: global proxy.process.http.avg_transactions_per_client_connection float
   :type: derivative

.. ts:stat:: global proxy.process.http.avg_transactions_per_server_connection float
   :type: derivative

.. ts:stat:: global proxy.process.http.total_transactions_time integer
   :type: counter
   :unit: seconds

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
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.connect_failed float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.other float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.possible_aborts float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.errors.pre_accept_hangups float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_fresh float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_fresh.process float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.hit_revalidated float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_changed float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_client_no_cache float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_cold float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.miss_not_cacheable float
   :type: counter
   :unit: seconds
   :ungathered:

.. ts:stat:: global proxy.process.http.transaction_totaltime.other.unclassified float
   :type: counter
   :unit: seconds
   :ungathered:

