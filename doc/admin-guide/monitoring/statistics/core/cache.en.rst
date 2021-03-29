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

.. _admin-stats-core-cache:

Cache
*****

.. ts:stat:: global proxy.node.cache.contents.num_docs integer
   :ungathered:

   Represents the number of documents currently residing in the cache.

.. ts:stat:: global proxy.process.cache_total_hits counter

   Represents the total number of cache lookups which have been satisfied by
   either the in-memory cache or the on-disk cache, since statistics collection
   began.

.. ts:stat:: global proxy.process.cache_total_misses counter

   Represents the total number of cache lookups which could not be satisfied by
   either the in-memory cache or the on-disk cache, and which required origin
   server revalidation or retrieval.

.. ts:stat:: global proxy.process.cache.bytes_total integer
.. ts:stat:: global proxy.process.cache.bytes_used integer
.. ts:stat:: global proxy.process.cache.directory_collision integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.direntries.total integer
.. ts:stat:: global proxy.process.cache.direntries.used integer
.. ts:stat:: global proxy.process.cache.evacuate.active integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.evacuate.failure integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.evacuate.success integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.frags_per_doc.1 integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.frags_per_doc.2 integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.frags_per_doc.3+ integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.gc_bytes_evacuated integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.gc_frags_evacuated integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.hdr_marshal_bytes integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.hdr_marshals integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.KB_read_per_sec float
.. ts:stat:: global proxy.process.cache.KB_write_per_sec float
.. ts:stat:: global proxy.process.cache.lookup.active integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.lookup.failure integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.lookup.success integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.percent_full integer
.. ts:stat:: global proxy.process.cache.pread_count integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.ram_cache.bytes_used integer
.. ts:stat:: global proxy.process.cache.ram_cache.hits integer
.. ts:stat:: global proxy.process.cache.ram_cache.misses integer
.. ts:stat:: global proxy.process.cache.ram_cache.total_bytes integer
.. ts:stat:: global proxy.process.cache.read.active integer
.. ts:stat:: global proxy.process.cache.read_busy.failure integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.read_busy.success integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.read.failure integer
.. ts:stat:: global proxy.process.cache.read_per_sec float
.. ts:stat:: global proxy.process.cache.read.success integer
.. ts:stat:: global proxy.process.cache.remove.active integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.remove.failure integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.remove.success integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.scan.active integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.scan.failure integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.scan.success integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.update.active integer
.. ts:stat:: global proxy.process.cache.update.failure integer
.. ts:stat:: global proxy.process.cache.update.success integer
.. ts:stat:: global proxy.process.cache.vector_marshals integer
.. ts:stat:: global proxy.process.cache.write.active integer
.. ts:stat:: global proxy.process.cache.write.backlog.failure integer
.. ts:stat:: global proxy.process.cache.write_bytes_stat integer
.. ts:stat:: global proxy.process.cache.write.failure integer
.. ts:stat:: global proxy.process.cache.write_per_sec float
.. ts:stat:: global proxy.process.cache.write.success integer

.. ts:stat:: global proxy.process.cache.span.errors.read integer

   The number of span read errors (counter).

.. ts:stat:: global proxy.process.cache.span.errors.write integer

   The number of span write errors (counter).

.. ts:stat:: global proxy.process.cache.span.failing integer

   The number of failing spans (gauge) - errors > 0 && errors < `proxy.config.cache.max_disk_errors`.

   `proxy.process.cache.span.failing` + `proxy.process.cache.span.offline` + `proxy.process.cache.span.online` = total number of spans.

.. ts:stat:: global proxy.process.cache.span.offline integer

   The number of offline spans (gauge) - errors >= `proxy.config.cache.max_disk_errors`.

   `proxy.process.cache.span.failing` + `proxy.process.cache.span.offline` + `proxy.process.cache.span.online` = total number of spans.

.. ts:stat:: global proxy.process.cache.span.online integer

   The number of online spans (gauge) - errors == 0.

   `proxy.process.cache.span.failing` + `proxy.process.cache.span.offline` + `proxy.process.cache.span.online` = total number of spans.


.. ts:stat:: global proxy.process.http.background_fill_bytes_aborted_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.background_fill_bytes_completed_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.background_fill_current_count integer
   :type: gauge
   :ungathered:

   Represents the current number of background fill

.. ts:stat:: global proxy.process.http.background_fill_total_count integer
   :type: counter
   :ungathered:

   Represents the total number of background fill

.. ts:stat:: global proxy.process.http.cache_deletes integer
.. ts:stat:: global proxy.process.http.cache_hit_fresh integer
.. ts:stat:: global proxy.process.http.cache_hit_ims integer
.. ts:stat:: global proxy.process.http.cache_hit_mem_fresh integer
.. ts:stat:: global proxy.process.http.cache_hit_revalidated integer
.. ts:stat:: global proxy.process.http.cache_hit_stale_served integer
.. ts:stat:: global proxy.process.http.cache_lookups integer
.. ts:stat:: global proxy.process.http.cache_miss_changed integer
.. ts:stat:: global proxy.process.http.cache_miss_client_no_cache integer
.. ts:stat:: global proxy.process.http.cache_miss_client_not_cacheable integer
.. ts:stat:: global proxy.process.http.cache_miss_cold integer
.. ts:stat:: global proxy.process.http.cache_miss_ims integer
.. ts:stat:: global proxy.process.http.cache_read_error integer
.. ts:stat:: global proxy.process.http.cache_read_errors integer
.. ts:stat:: global proxy.process.http.cache_updates integer
.. ts:stat:: global proxy.process.http.cache_write_errors integer
.. ts:stat:: global proxy.process.http.cache_writes integer
.. ts:stat:: global proxy.process.http.tcp_client_refresh_count_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_client_refresh_origin_server_bytes_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_client_refresh_user_agent_bytes_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_expired_miss_count_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_expired_miss_origin_server_bytes_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_expired_miss_user_agent_bytes_stat integer
   :ungathered:

.. ts:stat:: global proxy.process.http.tcp_hit_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_hit_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_hit_user_agent_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_hit_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_hit_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_hit_user_agent_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_miss_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_miss_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_ims_miss_user_agent_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_miss_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_miss_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_miss_user_agent_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_hit_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_hit_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_miss_count_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_miss_origin_server_bytes_stat integer
.. ts:stat:: global proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat integer
