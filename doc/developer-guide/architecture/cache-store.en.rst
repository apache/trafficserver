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

.. _developer-cache-store:

.. default-domain:: cpp

Cache Store
******************

Initialization
==============

:file:`storage.config` is read by :func:`Store::read_config` invoked from :code:`ink_cache_init`.

Types
=====

.. var:: size_t STORE_BLOCK_SIZE = 8192

   The metric for measuring the size of stripe storage allocation. Note this is very different from
   :var:`CACHE_BLOCK_SIZE` which is the metric for *object* allocation.

.. var:: size_t CACHE_BLOCK_SIZE = 512

   The metric for object storage allocation. The amount of storage allocated for an object in the
   cache is a multiple of this value.

.. class:: span_diskid_t

   Stores a 16 byte ID.

.. class:: Span

   :class:`Span` models a :term:`cache span`. This is a contiguous span of storage.

   .. member:: int64_t blocks

      Number of storage blocks in the span. See :var:`STORE_BLOCK_SIZE`.

   .. member:: int64_t offset

      Offset (in bytes)_ to the start of the span. This is used only if the base storage is a file.

   .. member:: span_diskid_t disk_id

      No idea what this is.

.. class:: Store

   A singleton containing all of the cache storage description.

   .. member:: unsigned n_disks_in_config

      The number of distinct devices in the configuration.

   .. member:: unsigned n_disks

      The number of valid and distinct devices in the configuration.

   .. member:: Span** disk

      List of spans.

   .. member:: char * read_config()
q
      Read :file:`storage.config` and initialize the base state of the instance. The return value is :code:`nullptr` on success and a nul-terminated error string on error.
