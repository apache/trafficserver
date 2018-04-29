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

.. uml::
   :align: center

   hide empty members

   CacheHostRecord *-- "*" Stripe : Vol >
   CacheHostRecord *-- "*" CacheVol : cp >
   CacheVol *-- "*" Stripe : Vol >

.. var:: size_t STORE_BLOCK_SIZE = 8192

   The storage unit for span and stripe metadata.

.. class:: CacheHostTable

   A container that maps from a FQDN to a :class:`CacheHostRecord`. This is constructed from
   the contents of :file:`hosting.config`.

   .. function:: void Match(char const * fqdn, int len, CacheHostResult * result)

      Search the table for a match for the hostname :arg:`fqdn`, a string of length :arg:`len`.
      If found the result is placed in :arg:`result`.

.. class:: CacheHostResult

   A wrapper for :class:`CacheHostRecord` used by :func:`CacheHostTable::Match`. This contains
   the set of cache volumes for the cache host record and is used to perform stripe assignment.

.. class:: CacheHostRecord

   A cache hosting record from :file:`hosting.config`.

   .. member:: CacheVol ** cp

      The cache volumes that are part of this cache host record.

   .. member:: Vol ** vols

      The stripes that are part of the cache volumes. This is the union over the stripes of
      :member:`CacheHostRecord::cp`

   .. member:: unsigned short * vol_hash_table

      The stripe assignment table. This is an array of indices in to
      :member:`CacheHostRecord::vols`.

   .. see: :class:`CacheHostTable`.

.. class:: OpenDir

   An open directory entry. It contains all the information of a
   :class:`Dir` plus additional information from the first :class:`Doc`.

.. class:: CacheVC

   A virtual connection class which accepts input for writing to cache.

   .. function:: int openReadStartHead(int event, Event* e)

      Performs the initial read for a cached object.

   .. function:: int openReadStartEarliest(int event, Event* e)

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

.. class:: Vol

   This represents a :term:`storage unit` inside a :term:`cache volume`.

   .. cpp:member:: off_t data_blocks

      The number of blocks of storage in the stripe.

   .. cpp:member:: int aggWrite(int event, void * e)

      Schedule the aggregation buffer to be written to disk.

   .. member:: off_t segments

      The number of segments in the volume. This will be roughly the total
      number of entries divided by the number of entries in a segment. It will
      be rounded up to cover all entries.

   .. member:: off_t buckets

      The number of buckets in the volume. This will be roughly the number of
      entries in a segment divided by ``DIR_DEPTH``. For currently defined
      values this is around 16,384 (2^16 / 4). Buckets are used as the targets
      of the index hash.

   .. member:: DLL<EvacuationBlock> evacuate

      Array of of :class:`EvacuationBlock` buckets. This is sized so there
      is one bucket for every evacuation span.

   .. member:: off_t len

      Length of stripe in bytes.

   .. member:: int evac_range(off_t low, off_t high, int evac_phase)

         Start an evacuation if there is any :class:`EvacuationBlock` in the range
         from :arg:`low` to :arg:`high`. Return ``0`` if no evacuation was started,
         non-zero otherwise.

.. class:: Doc

   Defined in :ts:git:`iocore/cache/P_CacheVol.h`.

   .. member:: uint32_t magic

      Validity check value. Set to ``DOC_MAGIC`` for a valid document.

   .. member:: uint32_t len

      The length of this segment including the header length, fragment table,
      and this structure.

   .. member:: uint64_t total_len

      Total length of the entire document not including meta data but including
      headers.

   .. member:: INK_MD5 first_key

      First index key in the document (the index key used to locate this object
      in the volume index).

   .. member:: INK_MD5 key

      The index key for this fragment. Fragment keys are computationally
      chained so that the key for the next and previous fragments can be
      computed from this key.

   .. member:: uint32_t hlen

      Document header (metadata) length. This is not the length of the HTTP
      headers.

   .. member:: uint8_t ftype

      Fragment type. Currently only ``CACHE_FRAG_TYPE_HTTP`` is used. Other
      types may be used for cache extensions if those are ever implemented.

   .. member:: uint24_t flen

      Fragment table length, if any. Only the first :class:`Doc` in an object should
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

   .. member:: uint32_t sync_serial

      Unknown.

   .. member:: uint32_t write_serial

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

   A description of a span stripe (Vol) block . This is a serialized data structure.

   .. member:: uint64_t offset

      Offset in the span of the start of the span stripe (Vol) block, in bytes.

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


.. class:: DiskVolBlockQueue

   .. member:: DiskVolBlock* b

   .. member:: int new_block

      Indicates if this is a new stripe rather than an existing one. In case a stripe is new ATS decides to clear that stripe(:class:`Vol`)

   .. member:: LINK<DiskVolBlockQueue> link


.. class:: DiskVol

   Describes the Disk that contains the stripe identified by vol_number. This class also contains the queue
   containing all the DiskVolBlock

   .. member:: int num_volblocks

      Number of blocks in the stripe identified by vol_number

   .. member:: int vol_number

      Identification number of the stripe (:class:`Vol`)

   .. member:: uint64_t size

      Size of the stripe

   .. member:: CacheDisk* disk

      The disk containing the stripe

   .. member:: Queue<DiskVolBlockQueue> dpb_queue

.. enum:: CacheType

   .. enumerator:: HTTP

   .. enumerator:: Stream

.. class:: CacheVol

   A :term:`cache volume` as described in :file:`volume.config`. This class represents a single volume. :class:`CacheVol` comprises of stripes spread across Spans(disks)

   .. member:: int volume_number

      indentification number of this volume

   .. member:: int scheme

      An enumeration of value :enumerator:`CacheType::HTTP` or :enumerator:`CacheType::Stream`.

   .. member:: off_t size


   .. member:: int num_vols

      Number of stripes(:class:`Vol`) contained in this volume

   .. member:: Vol** vols

      :class:`Vol` represents a single stripe in the disk. vols contains all the stripes this volume is made up of

   .. member:: DiskVol** disk_vols

      disk_vols contain references to the disks of all the stripes in this volume

   .. member:: LINK<CacheVol> link

   .. member:: RecRawStatBlock vol_rsb

      per volume stat

.. class:: ConfigVol

   This class represents an individual volume.

   .. member:: int number

      Identification number of the volume

   .. member:: CacheType scheme

   .. member:: off_t size

   .. member:: bool in_percent

      Used as an indicator if the volume is part of the overall volumes created by ATS

   .. member:: int percent

   .. member:: CacheVol* cachep

   .. member:: LINK<ConfigVol> link

.. class:: ConfigVolumes

    .. member:: int num_volumes

       Total number of volumes specified in volume.config

    .. member:: int num_http_volumes

       Total number of volumes scpecified in volume.config for HTTP scheme

    .. member:: Queue<ConfigVol> cp_queue

.. class:: Cache

   Base object for a cache.

   .. member:: CacheHostRecord hosttable

      A generic class:`CacheHostRecord` that contains all cache volumes that are not explicitly
      assigned in :file:`hosting.config`.

   .. function:: Vol * key_to_vol(const char * key, const char * host, int host_len)

      Compute the stripe (:code:`Vol*`) for a cache :arg:`key` and :arg:`host`. The :arg:`host` is
      used to find the appropriate :class:`CacheHostRecord` instance. From there the stripe
      assignment slot is determined by taking bits 64..83 (20 bits) of the cache :arg:`key` modulo
      the stripe assignment array count (:code:`VOL_HASH_TABLE_SIZE`). These bits are the third 32
      bit slice of the :arg:`key` less the bottom :code:`DIR_TAG_WIDTH` (12) bits.

.. rubric:: Footnotes

.. [#fragment-offset-table]

   Changed in version 3.2.0. This previously resided in the first ``Doc`` but
   that caused different alternates to share the same fragment table.
