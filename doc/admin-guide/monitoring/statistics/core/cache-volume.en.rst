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

.. _admin-stats-core-cache-volume:

Cache Volume
************

Each configured :term:`cache volume` receives a complete set of statistics from
|TS|. They are differentiated from each other by the incrementing number that is
part of the statistic name. The volume number begins at :literal:`0` and
increments by :literal:`1` for each additional volume.

The statistics are documented in this section using the default volume number in
a configuration with only one cache volume: :literal:`0`.

.. ts:stat:: global proxy.process.cache.volume_0.bytes_total integer
   :type: gauge
   :unit: bytes

   Represents the total number of bytes allocated for the cache volume.

.. ts:stat:: global proxy.process.cache.volume_0.bytes_used integer
   :type: gauge
   :unit: bytes

   Represents the number of bytes in this cache volume which are occupied by
   cache objects.

.. ts:stat:: global proxy.process.cache.volume_0.directory_collision integer
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.direntries.total integer
   :type: gauge

   Represents the total number of directory entries which have been allocated
   in this cache volume.

.. ts:stat:: global proxy.process.cache.volume_0.direntries.used integer
   :type: gauge

   Represents the number of allocated directory entries in this cache volume
   which are in use.

.. ts:stat:: global proxy.process.cache.volume_0.evacuate.active integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.evacuate.failure integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.evacuate.success integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.frags_per_doc.1 integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.frags_per_doc.2 integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.frags_per_doc.3+ integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.gc_bytes_evacuated integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.gc_frags_evacuated integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.hdr_marshal_bytes integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.hdr_marshals integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.lookup.active integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.lookup.failure integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.lookup.success integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.percent_full integer
   :type: gauge
   :unit: percent

.. ts:stat:: global proxy.process.cache.volume_0.pread_count integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.ram_cache.bytes_used integer
   :type: gauge
   :unit: bytes

.. ts:stat:: global proxy.process.cache.volume_0.ram_cache.hits integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.ram_cache.misses integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.ram_cache.total_bytes integer
   :type: gauge
   :unit: bytes

.. ts:stat:: global proxy.process.cache.volume_0.read.active integer
   :type: gauge

.. ts:stat:: global proxy.process.cache.volume_0.read_busy.failure integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.read_busy.success integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.read.failure integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.read.success integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.remove.active integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.remove.failure integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.remove.success integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.scan.active integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.scan.failure integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.scan.success integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.update.active integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.update.failure integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.update.success integer
   :type: counter
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.vector_marshals integer
   :type: gauge
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.write.active integer
   :type: gauge

.. ts:stat:: global proxy.process.cache.volume_0.write.backlog.failure integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.write_bytes_stat integer
   :type: counter
   :unit: bytes
   :ungathered:

.. ts:stat:: global proxy.process.cache.volume_0.write.failure integer
   :type: counter

.. ts:stat:: global proxy.process.cache.volume_0.write.success integer
   :type: counter

