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

.. _core_cache_functions:

Core Cache
**********

Core Cache Constants
====================

.. c:macro:: EVACUATION_SIZE

   The size of the contiguous area to check for evacuation.

Core Cache Types
================

.. cpp:class:: CacheKey

  The hash value for a cache object. Currently a 128 bit MD5 hash.
  
.. cpp:class:: EvacuationBlock

   A range of content to be evacuated.

.. cpp:class:: Vol

   A representation of a :term:`cache stripe`.

   .. cpp:member:: off_t data_blocks

      The number of blocks of storage in the stripe.

   .. cpp:member:: DLL<EvacuationBlock> evacuate

      A list of blocks to evacuate.

   .. cpp:member:: int aggWrite(int event, void * e)

      Schedule the aggregation buffer to be written to disk.
      
.. cpp:class:: CacheProcessor

   The singleton cache management object. This handles threads and global initialization for the cache.
   
   .. cpp:member:: int start(int n_threads, size_t stacksize)
   
      Starts the cache processing threads, :arg:`n_threads` are created each with a stack of size :arg:`stacksize`.

.. cpp:class:: Span

   A representation of a unit of cache storage, a single physical device, file, or directory.

.. cpp:class:: Store

   A representation of a collection of cache storage.
   
   .. cpp:member:: Span ** disk
   
      List of :cpp:class:`Span` instances that describe the physical storage.

.. cpp:class:: CacheDisk

   A representation of the physical device used for a :cpp:class:`Span`.
  
.. cpp:class:: CacheHostRecord

   A record from :file:`hosting.config`.

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
