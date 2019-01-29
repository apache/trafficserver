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

.. include:: ../common.defs

.. _glossary:

Glossary
********

.. glossary::
   :sorted:

   continuation
      A callable object that contains state. These are are mechanism used by
      |TS| to implement callbacks and continued computations. Continued
      computations are critical to efficient processing of traffic because by
      avoiding any blocking operations that wait on external events. In any
      such case a continuation is used so that other processing can continue
      until the external event occurs. At that point the continuation is
      invoked to continue the suspended processing. This can be considered
      similar to co-routines.

   event loop
      Code that executes callbacks in continuations from a queue of events.

   event thread
      A thread created by |TS| that has an :term:`event loop`. Event loops drive activity in |TS|
      and are responsible for all network I/O handling, hook processing, and scheduled events.

   header heap
      A heap to manage transaction local memory for HTTP headers.

   session
      A single connection from a client to Traffic Server, covering all
      requests and responses on that connection. A session starts when the
      client connection opens, and ends when the connection closes.

   transaction
      A client request and response, either from the origin server or from the
      cache. A transaction begins when |TS| receives a request, and ends when
      |TS| sends the response.

   cache volume
      A user defined unit of persistent storage for the cache. Cache volumes
      are defined in :file:`volume.config`. A cache volume is by default spread
      across :term:`cache span`\ s to increase robustness. Each section of a
      cache volume on a specific cache span is a :term:`cache stripe`.

   cache stripe
      A homogenous, persistent store for the cache in a single
      :term:`cache span`. A stripe always resides entirely on a single physical
      device and is treated as an undifferentiated span of bytes. This is the
      smallest independent unit of storage.

   cache span
      The physical storage described by a single line in
      :file:`storage.config`.

   cache key
      A byte sequence that is a globally unique identifier for an
      :term:`object <cache object>` in the cache. By default the URL for the
      object is used.

   cache ID
      A 128 bit value used as a fixed sized identifier for an object in the
      cache. This is computed from the :term:`cache key` using the
      `MD5 hashing function <http://www.openssl.org/docs/crypto/md5.html>`_.

   cache tag
      The bottom few bits (12 currently) of the :term:`cache ID`. This is used
      in the :ref:`cache directory <cache-directory>` for a preliminary
      identity check before going to disk.

   cache object
      The minimal self contained unit of data in the cache. Cache objects are
      the stored version of equivalent content streams from an origin server. A
      single object can have multiple variants called
      :term:`alternates <alternate>`.

   alternate
      A variant of a :term:`cache object`. This was originally created to
      handle the `VARY mechanism
      <http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.44>`_
      but has since been used for additional purposes. All alternates of an
      object must be equivalent in some manner. That is, they are alternate
      forms of the same stream. The most common example is having normal and
      compressed versions of the stream.

   storage unit
      Obsolete term for :term:`cache span`.

   revalidation
      Verifying that a currently cached object is still valid. This is usually
      done using an `If-Modified-Since
      <http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.25>`_
      request which allows the origin server to validate the content without
      resending the content.

   write cursor
      The location in a :term:`cache stripe` where new data is written.

   directory segment
      A contiguous group of :term:`buckets <directory bucket>`. Each
      :term:`cache stripe` has a set of segments all of which have the same
      number of buckets, although the number of buckets per segment can vary
      between cache stripes. Segments are administrative in purpose to minimize
      the size of free list and hash bucket pointers.

   directory bucket
      A contiguous fixed sized group of :term:`directory entries
      <directory entry>`. This is used for hash bucket maintenance
      optimization.

   directory entry
      An in memory entry that describes a :term:`cache fragment`.

   cache fragment
      The unit of storage in the cache. All reads from the cache always read
      exactly one fragment. Fragments may be written in groups, but every write
      is always an integral number of fragments. Each fragment has a
      corresponding :term:`directory entry` which describes its location in the
      cache storage.

   object store
      The database of :term:`cache objects <cache object>`.

   fresh
      The state of a :term:`cache object` which can be served directly from the
      the cache in response to client requests. Fresh objects have not met or
      passed their :term:`origin server` defined expiration time, nor have they
      reached the algorithmically determined :term:`stale` age.

   stale
      The state of a :term:`cache object` which is not yet expired, but has
      reached an algorithmically determined age at which the
      :term:`origin server` will be contacted to :term:`revalidate
      <revalidation>` the freshness of the object. Contrast with :term:`fresh`.

   origin server
      An HTTP server which provides the original source of content being cached
      by Traffic Server.

   cache partition
      A subdivision of the cache storage in |TS| which is dedicated to objects
      for specific protocols, origins, or other rules. Defining and managing
      cache partitions is discussed in :ref:`partitioning-the-cache`.

   global plugin
      A plugin which operates on all transactions. Contrast with
      :term:`remap plugin`.

   remap plugin
      A plugin which operates only on transactions matching specific remap
      rules as defined in :file:`remap.config`. Contrast with
      :term:`global plugin`.

   variable sized class
      A class where the instances vary in size. This is done by allocating a block of memory at least
      as large as the class and then constructing a class instance at the start of the block. The
      class must be provided the size of this extended memory during construction and is presumed
      to use it as part of the instance. This generally requires calling a helper function to
      create the instance and extra care when de-allocating.
