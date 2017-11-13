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
.. _developer-cache-data-structures:

Data Structures
***************

.. class:: OpenDir

   An open directory entry. It contains all the information of a
   :class:`Dir` plus additional information from the first :class:`Doc`.

.. class:: CacheVC

   A virtual connection class which accepts input for writing to cache.

.. function:: int CacheVC::openReadStartHead(int event, Event* e)

   Performs the initial read for a cached object.

.. function:: int CacheVC::openReadStartEarliest(int event, Event* e)

   Performs the initial read for an :term:`alternate` of an object.

.. class:: HttpTunnel

   Data transfer driver. This contains a set of *producers*. Each producer is
   connected to one or more *consumers*. The tunnel handles events and buffers
   so that data moves from producers to consumers. The data, as much as
   possible, is kept in reference counted buffers so that copies are done only
   when the data is modified or for sources (which acquire data from outside
   |TS|) and sinks (which move data to outside |TS|).

.. class:: CacheControlResult

   Holds the data from a line in :file:`cache.config`.

.. class:: CacheHTTPInfoVector

   Defined in :ts:git:`iocore/cache/P_CacheHttp.h`. This is an array of :class:`HTTPInfo`
   objects and serves as the respository of information about alternates of an
   object. It is marshaled as part of the metadata for an object in the cache.

.. class:: HTTPInfo

   Defined in :ts:git:`proxy/hdrs/HTTP.h`.

   This class is a wrapper for :class:`HTTPCacheAlt`. It provides the
   external API for accessing data in the wrapped class. It contains only a
   pointer (possibly ``NULL``) to an instance of the wrapped class.

.. class:: CacheHTTPInfo

   A typedef for :class:`HTTPInfo`.

.. class:: HTTPCacheAlt

   Defined in :ts:git:`proxy/hdrs/HTTP.h`.

   This is the metadata for a single :term:`alternate` for a cached object. It
   contains, among other data, the following:

   * The key for the earliest ``Doc`` of the alternate.

   * The request and response headers.

   * The fragment offset table.[#fragment-offset-table]_

   * Timestamps for request and response from :term:`origin server`.

.. class:: EvacuationBlock

    Record for evacuation.

.. class:: Vol

   This represents a :term:`storage unit` inside a :term:`cache volume`.

   .. member:: off_t Vol::segments

      The number of segments in the volume. This will be roughly the total
      number of entries divided by the number of entries in a segment. It will
      be rounded up to cover all entries.

   .. member:: off_t Vol::buckets

      The number of buckets in the volume. This will be roughly the number of
      entries in a segment divided by ``DIR_DEPTH``. For currently defined
      values this is around 16,384 (2^16 / 4). Buckets are used as the targets
      of the index hash.

   .. member:: DLL\<EvacuationBlock\> Vol::evacuate

      Array of of :class:`EvacuationBlock` buckets. This is sized so there
      is one bucket for every evacuation span.

   .. member:: off_t len

      Length of stripe in bytes.

   .. member:: int evac_range(off_t low, off_t high, int evac_phase)

         Start an evacuation if there is any :class:`EvacuationBlock` in the range
         from :arg:`low` to :arg:`high`. Return ``0`` if no evacuation was started,
         non-zero otherwise.

.. class:: CacheVol

   A :term:`cache volume` as described in :file:`volume.config`.

.. class:: Doc

   Defined in :ts:git:`iocore/cache/P_CacheVol.h`.

   .. member:: uint32_t Doc::magic

      Validity check value. Set to ``DOC_MAGIC`` for a valid document.

   .. member:: uint32_t Doc::len

      The length of this segment including the header length, fragment table,
      and this structure.

   .. member:: uint64_t Doc::total_len

      Total length of the entire document not including meta data but including
      headers.

   .. member:: INK_MD5 Doc::first_key

      First index key in the document (the index key used to locate this object
      in the volume index).

   .. member:: INK_MD5 Doc::key

      The index key for this fragment. Fragment keys are computationally
      chained so that the key for the next and previous fragments can be
      computed from this key.

   .. member:: uint32_t Doc::hlen

      Document header (metadata) length. This is not the length of the HTTP
      headers.

   .. member:: uint8_t Doc::ftype

      Fragment type. Currently only ``CACHE_FRAG_TYPE_HTTP`` is used. Other
      types may be used for cache extensions if those are ever implemented.

   .. member:: uint24_t Doc::flen

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

      Removed as of version 3.3.0. [#fragment-offset-table]_

   .. member:: uint32_t Doc::sync_serial

      Unknown.

   .. member:: uint32_t Doc::write_serial

      Unknown.

   .. member:: uint32_t pinned

      Flag and timer for pinned objects.

   .. member:: uint32_t checksum

      Unknown.

.. class:: DiskHeader

   Header for a span. This is a serialized data structure.

   .. member:: unsigned int magic

      Holds a magic value :code:``DISK_HEADER_MAGIC`` to indicate the span is valid and initialized.

   .. member:: unsigned int num_volumes

      Number of cache volumes containing stripes in this span.

   .. member:: unsigned int num_free

      The number of span blocks defined but not in use.

   .. member:: unsigned int num_used

      The number of span blocks in use by stripes.

   .. member:: unsigned int num_diskvol_blks

      The number of span blocks.

   .. member:: uint64_t num_blocks

      The number of volume blocks in the span.

   .. member:: DiskVolBlock vol_info[1]

      A flexible array. The actual length of this array is :code:`num_diskvol_blks` and each element describes
      a span block.

.. class:: DiskVolBlock

   A description of a span block. This is a serialized data structure.

   .. member:: uint64_t offset

      Offset in the span of the start of the span block, in bytes.

   .. member:: uint64_t len

      Length of the span block in store blocks.

   .. member:: int number

      The cache volume index for this span block.

   .. member:: unsigned int __attribute__((bitfield_3)) type

      Type of the span block.

   .. member:: unsigned int __attribute__((bitfield_1)) free

      In use or free flag - set if the span block is not in use by a cache volume.


.. class:: VolHeaderFooter

   .. member:: unsigned int magic

      Container for a magic value, ``VOL_MAGIC``, to indicate the instance is valid.

   .. member:: VersionNumber version

      Version of the instance.

   .. member:: time_t create_time

      Epoch time when the stripe was created.

   .. member:: off_t write_pos

      Position of the write cursor, as a byte offset in the stripe.

   .. member:: off_t last_write_pos

      Location of the write cursor of the most recently completed disk write.

   .. member:: off_t agg_pos

      The byte offset in the stripe where the current aggregation buffer will be written.

   .. member:: uint32_t generation

      Generation of this instance.

   .. member:: uint32_t phase

   .. member:: uint32_t cycle

   .. member:: uint32_t sync_serial

   .. member:: uint32_t write_serial

   .. member:: uint32_t dirty

   .. member:: uint32_t unused

   .. member:: uint16_t freelist[1]

      An array of directory entry indices. Each element is the directory entry of the start of the free list
      for a segment, in the same order as the segments in the directory.

.. rubric:: Footnotes

.. [#fragment-offset-table]

   Changed in version 3.2.0. This previously resided in the first ``Doc`` but
   that caused different alternates to share the same fragment table.
