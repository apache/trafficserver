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

.. include:: ../../common.defs
.. default-domain:: cpp

.. _core_cache_functions:

Core Cache
**********

Core Cache Constants
====================

.. c:macro:: EVACUATION_SIZE

   The size of the contiguous area to check for evacuation.

Core Cache Types
================

.. type:: span_diskid_t

   Stores a 16 byte ID.

.. cpp:class:: CacheKey

  The hash value for a cache object. Currently a 128 bit MD5 hash.

.. cpp:class:: EvacuationBlock

   A range of content to be evacuated.

.. cpp:class:: CacheProcessor

   The singleton cache management object. This handles threads and global initialization for the cache.

   .. cpp:member:: int start(int n_threads, size_t stacksize)

      Starts the cache processing threads, :arg:`n_threads` are created each with a stack of size :arg:`stacksize`.

.. cpp:class:: Span

   :class:`Span` models a :term:`cache span`. This is a contiguous span of storage.

   .. member:: int64_t blocks

      Number of storage blocks in the span. See :var:`STORE_BLOCK_SIZE`.

   .. member:: int64_t offset

      Offset (in bytes)_ to the start of the span. This is used only if the base storage is a file.

   .. member:: span_diskid_t disk_id

      No idea what this is.

.. cpp:class:: Store

   A singleton containing all of the cache storage description.

   .. member:: unsigned n_disks_in_config

      The number of distinct devices in the configuration.

   .. member:: unsigned n_disks

      The number of valid and distinct devices in the configuration.

   .. member:: Span** disk

      List of spans.

   .. function:: char * read_config()

      Read :file:`storage.config` and initialize the base state of the instance. The return value is :code:`nullptr` on success and a nul-terminated error string on error.

.. cpp:class:: CacheDisk

   A representation of the physical device used for a :cpp:class:`Span`.

Core Cache Functions
====================

.. cpp:function:: int dir_probe(const CacheKey * key, Vol * d, Dir * result, Dir ** last_collision)

  Probe the stripe directory for a candidate directory entry.

.. cpp:function:: void build_vol_hash_table(CacheHostRecord * r)

   Based on the configuration record :arg:`r`, construct the global stripe assignment table.

.. cpp:function:: int cplist_reconfigure()

   Rebuild the assignment of stripes to volumes.

.. cpp:function:: void ink_cache_init(ModuleVersion v)

   Top level cache initialization logic.
