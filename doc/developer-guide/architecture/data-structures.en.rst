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

.. _developer-cache-data-structures:

Data Structures
***************

.. cpp:class:: OpenDir

   An open directory entry. It contains all the information of a
   :cpp:class:`Dir` plus additional information from the first :cpp:class:`Doc`.

.. cpp:class:: CacheVC

   A virtual connection class which accepts input for writing to cache.

.. cpp:function:: int CacheVC::openReadStartHead(int event, Event* e)

   Performs the initial read for a cached object.

.. cpp:function:: int CacheVC::openReadStartEarliest(int event, Event* e)

   Performs the initial read for an :term:`alternate` of an object.

.. cpp:class:: HttpTunnel

   Data transfer driver. This contains a set of *producers*. Each producer is
   connected to one or more *consumers*. The tunnel handles events and buffers
   so that data moves from producers to consumers. The data, as much as
   possible, is kept in reference counted buffers so that copies are done only
   when the data is modified or for sources (which acquire data from outside
   |TS|) and sinks (which move data to outside |TS|).

.. cpp:class:: CacheControlResult

   Holds the data from a line in :file:`cache.config`.

.. cpp:class:: CacheHTTPInfoVector

   Defined in |P-CacheHttp.h|_. This is an array of :cpp:class:`HTTPInfo`
   objects and serves as the respository of information about alternates of an
   object. It is marshaled as part of the metadata for an object in the cache.

.. cpp:class:: HTTPInfo

   Defined in |HTTP.h|_.

   This class is a wrapper for :cpp:class:`HTTPCacheAlt`. It provides the
   external API for accessing data in the wrapped class. It contains only a
   pointer (possibly ``NULL``) to an instance of the wrapped class.

.. cpp:class:: CacheHTTPInfo

   A typedef for :cpp:class:`HTTPInfo`.

.. cpp:class:: HTTPCacheAlt

   Defined in |HTTP.h|_.

   This is the metadata for a single :term:`alternate` for a cached object. It
   contains, among other data, the following:

   * The key for the earliest ``Doc`` of the alternate.

   * The request and response headers.

   * The fragment offset table.[#fragment-offset-table]_

   * Timestamps for request and response from :term:`origin server`.

.. cpp:class:: EvacuationBlock

    Record for evacuation.

.. cpp:class:: Vol

   This represents a :term:`storage unit` inside a :term:`cache volume`.

   .. cpp:member:: off_t Vol::segments

      The number of segments in the volume. This will be roughly the total
      number of entries divided by the number of entries in a segment. It will
      be rounded up to cover all entries.

   .. cpp:member:: off_t Vol::buckets

      The number of buckets in the volume. This will be roughly the number of
      entries in a segment divided by ``DIR_DEPTH``. For currently defined
      values this is around 16,384 (2^16 / 4). Buckets are used as the targets
      of the index hash.

   .. cpp:member:: DLL\<EvacuationBlock\> Vol::evacuate

      Array of of :cpp:class:`EvacuationBlock` buckets. This is sized so there
      is one bucket for every evacuation span.

   .. cpp:member:: off_t len

      Length of stripe in bytes.

.. cpp:function:: int Vol::evac_range(off_t low, off_t high, int evac_phase)

   Start an evacuation if there is any :cpp:class:`EvacuationBlock` in the range
   from :arg:`low` to :arg:`high`. Return ``0`` if no evacuation was started,
   non-zero otherwise.

.. cpp:class:: CacheVol

   A :term:`cache volume` as described in :file:`volume.config`.

.. cpp:class:: Doc

   Defined in |P-CacheVol.h|_.

   .. cpp:member:: uint32_t Doc::magic

      Validity check value. Set to ``DOC_MAGIC`` for a valid document.

   .. cpp:member:: uint32_t Doc::len

      The length of this segment including the header length, fragment table,
      and this structure.

   .. cpp:member:: uint64_t Doc::total_len

      Total length of the entire document not including meta data but including
      headers.

   .. cpp:member:: INK_MD5 Doc::first_key

      First index key in the document (the index key used to locate this object
      in the volume index).

   .. cpp:member:: INK_MD5 Doc::key

      The index key for this fragment. Fragment keys are computationally
      chained so that the key for the next and previous fragments can be
      computed from this key.

   .. cpp:member:: uint32_t Doc::hlen

      Document header (metadata) length. This is not the length of the HTTP
      headers.

   .. cpp:member:: uint8_t Doc::ftype

      Fragment type. Currently only ``CACHE_FRAG_TYPE_HTTP`` is used. Other
      types may be used for cache extensions if those are ever implemented.

   .. cpp:member:: uint24_t Doc::flen

      Fragment table length, if any. Only the first ``Doc`` in an object should
      contain a fragment table.

      The fragment table is a list of offsets relative to the HTTP content (not
      counting metadata or HTTP headers). Each offset is the byte offset of the
      first byte in the fragment. The first element in the table is the second
      fragment (what would be index 1 for an array). The offset for the first
      fragment is of course always zero and so not stored. The purpose of this
      is to enable a fast seek for range requests. Given the first ``Doc`` the
      fragment containing the first byte in the range can be computed and loaded
      directly without further disk access.

      Removed as of version 3.3.0.

   .. cpp:member:: uint32_t Doc::sync_serial

      Unknown.

   .. cpp:member:: uint32_t Doc::write_serial

      Unknown.

   .. cpp:member:: uint32_t pinned

      Flag and timer for pinned objects.

   .. cpp:member:: uint32_t checksum

      Unknown.

.. cpp:class:: VolHeaderFooter

.. rubric:: Footnotes

.. [#fragment-offset-table]

   Changed in version 3.2.0. This previously resided in the first ``Doc`` but
   that caused different alternates to share the same fragment table.

