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

.. _developer-cache-ram-cache:


RAM Cache
*********

New RAM Cache Algorithm (CLFUS)
===============================

The new RAM Cache uses ideas from a number of cache replacement policies and
algorithms, including LRU, LFU, CLOCK, GDFS and 2Q, called CLFUS (Clocked Least
Frequently Used by Size). It avoids any patented algorithms and includes the
following features:

* Balances Recentness, Frequency and Size to maximize hit rate (not byte hit
  rate).

* Is Scan Resistant and extracts robust hit rates even when the working set does
  not fit in the RAM Cache.

* Supports compression at 3 levels: fastlz, gzip (libz), and xz (liblzma).
  Compression can be moved to another thread.

* Has very low CPU overhead, only slightly more than a basic LRU. Rather than
  using an O(lg n) heap, it uses a probabilistic replacement policy for O(1)
  cost with low C.

* Has relatively low memory overhead of approximately 200 bytes per object in
  memory.

The rationale for emphasizing hit rate over byte hit rate is that the overhead
of pulling more bytes from secondary storage is low compared to the cost of a
request.

The RAM Cache consists of an object hash fronting 2 LRU/CLOCK lists and a *seen*
hash table. The first cached list contains objects in memory, while the second
contains a history of objects which have either recently been in memory or are
being considered for keeping in memory. The *seen* hash table is used to make
the algorithm scan resistant.

The list entries record the following information:

============== ================================================================
Value          Description
============== ================================================================
key            16 byte unique object identifier
auxkeys        8 bytes worth of version number (in our system, the block in the
               partition). When the version of an object changes old entries are
               purged from the cache.
hits           Number of hits within this clock period.
size           size of the object in the cache.
len            Length of the object, which differs from *size* because of
               compression and padding).
compressed_len Compressed length of the object.
compressed     Compression type, or ``none`` if no compression. Possible types
               are: *fastlz*, *libz*, and *liblzma*.
uncompressible Flag indicating that content cannot be compressed (true), or that
               it mat be compressed (false).
copy           Whether or not this object should be copied in and copied out
               (e.g. HTTP HDR).
LRU link
HASH link
IOBufferData   Smart point to the data buffer.
============== ================================================================

The interface to the cache is *Get* and *Put* operations. Get operations check
if an object is in the cache and are called on a read attempt. The Put operation
decides whether or not to cache the provided object in memory. It is called
after a read from secondary storage.

Seen Hash
=========

The *Seen List* becomes active after the *Cached* and *History* lists become
full following a cold start. The purpose is to make the cache scan resistant,
which means that the cache state must not be affected at all by a long sequence
Get and Put operations on objects which are seen only once. This is essential,
and without it not only would the cache be polluted, but it could lose critical
information about the objects that it cares about. It is therefore essential
that the Cache and History lists are not affected by Get or Put operations on
objects seen the first time. The Seen Hash maintains a set of 16 bit hash tags,
and requests which do not hit in the object cache (are in the Cache List or
History List) and do not match the hash tag result in the hash tag being updated
but are otherwise ignored. The Seen Hash is sized to approximately the number of
objects in the cache in order to match the number that are passed through it
with the CLOCK rate of the Cached and History Lists.

Cached List
===========

The *Cached List* (``_lru[0]`` in ``RamCacheCLFUS.cc``) holds the objects
actually resident in memory. New entries are inserted into a FIFO queue and a
hit reinserts the object at the tail. The interesting work happens when an
object is considered for insertion (a *Put*, after a read from secondary
storage). A check is first made against the object hash to see if the object is
already in the Cached List or the History List.

Each object is ranked by a weighted frequency, its *value*::

   CACHE_VALUE = (hits + 1) / (size + ENTRY_OVERHEAD)

Smaller and more frequently used objects rank higher, which is what is meant by
least frequently used *by size*. The value of a candidate is compared against
``_average_value``, an exponential moving average of the value of the objects
passed over for replacement -- in effect a floating admission bar.

.. note::

   ``CACHE_VALUE`` must be evaluated in floating point. Because ``hits`` is
   small and ``size`` is large, computing ``(hits + 1) / (size + overhead)`` in
   integer arithmetic truncates to ``0`` for every normal object, which silently
   collapses CLFUS to FIFO. This was the regression introduced in GitHub PR
   #11733; the division is now forced to floating point.

When a *Put* finds the incoming object in the History List, its value is
compared against the least recently used members of the Cached List. The
candidate must be worth at least as many bytes of currently cached objects as it
displaces. Each time an object is considered for replacement the CLOCK advances.
If the candidate wins it is moved into the Cached List and the objects it
displaces are removed from memory, their (data-less) list entries moving to the
History List; if it loses it is returned to the History List. Objects passed
over for replacement (at least one) have their ``hits`` reduced and are
reinserted -- this is the CLOCK (second chance) on the Cached List.

Aging the cached list
---------------------

Frequency counts on resident objects would otherwise only ever grow, so an
object that was hot days ago keeps winning replacement long after it has gone
cold, and the cache cannot follow a changing working set. To prevent this, once
per *turnover* (one *Put* for every resident object) ``_age_resident()`` halves
every resident ``hits`` count *and* halves ``_average_value`` in the same pass.
Halving the bar matters: ``_average_value`` is otherwise updated only inside the
replacement loop, which a low-value candidate never reaches, so without it the
decayed counts would be invisible to the admission decision and a warming
working set could never break in.

History List
============

The *History List* (``_lru[1]``) is a bounded record of keys recently evicted
from, or considered for, the Cached List. Its entries carry no data (the
``IOBufferData`` pointer is null); they exist so that an object requested again
soon after eviction can be cheaply re-admitted, and so a newly seen object can
accumulate enough value to earn admission before it is forgotten.

Each CLOCK tick (``_tick()``, run once per eviction) ages the oldest History
entry -- halving its ``hits`` -- and *keeps* it, freeing entries only to hold the
list at its target size, ``_objects / HISTORY_DIVISOR + HISTORY_HYSTERIA``. An
earlier version freed an entry the moment its aged ``hits`` reached ``0``, which
held the list nearly empty and denied re-requested objects their second chance.

The list is deliberately capped well below a full cache-worth
(``HISTORY_DIVISOR`` defaults to 4): it only needs to remember recent candidates
long enough to be requested again, and a full cache-worth of history entries is a
large memory cost (see `Memory overhead`_) for caches holding many small objects.

Following a shifting working set
================================

The combination of the bounded, persistent History List and resident aging is
what lets CLFUS track a working set that changes over time. New objects survive
in history long enough to prove themselves and be admitted, while the frequency
advantage of the previous working set decays until its members fall below the
admission bar and are evicted. The ``ram_cache_adaptivity`` (an abrupt change of
the entire hot set) and ``ram_cache_drift`` (a gradually rolling working set)
regression tests in ``CacheTest.cc`` exercise this and compare CLFUS against the
simpler LRU RAM cache.

Memory overhead
===============

Every object in the Cached List has a resident list entry; this per-object
overhead (roughly ``ENTRY_OVERHEAD``) is counted against
``proxy.config.cache.ram_cache.size``. Every object in the History List has a
list entry too (about 88 bytes), but these are **not** counted against the
configured size -- they are memory the process uses in addition to it.

Because the overhead is per object, it is largest for a big cache holding many
small objects. ``HISTORY_DIVISOR`` bounds the History List to roughly
``_objects / 4`` entries to keep this cost modest: for example a 32 GB cache of
1 KB objects holds about 32 million resident objects and therefore about 8
million history entries (~700 MB), rather than ~2.8 GB at a full cache-worth.

Compression and Decompression
=============================

Compression is performed by a background operation (currently called as part of
Put) which maintains a pointer into the Cached List and runs toward the head
compressing entries. Decompression occurs on demand during a Get. In the case
of objects tagged ``copy``, the compressed version is reinserted in the LRU
since we need to make a copy anyway. Those not tagged ``copy`` are inserted
uncompressed in the hope that they can be reused in uncompressed form. This is
a compile time option and may be something we want to change.

There are 3 algorithms and levels of compression (speed on an Intel i7 920
series processor using one thread):

======= ================ ================== ====================================
Method  Compression Rate Decompression Rate Notes
======= ================ ================== ====================================
fastlz  173 MB/sec       442 MB/sec         Basically free since disk or network
                                            will limit first; ~53% final size.
libz    55 MB/sec        234 MB/sec         Almost free, particularly
                                            decompression; ~37% final size.
liblzma 3 MB/sec         50 MB/sec          Expensive; ~27% final size.
======= ================ ================== ====================================

These are ballpark numbers, and your millage will vary enormously. JPEG, for
example, will not compress with any of these (or at least will only do so at
such a marginal level that the cost of compression and decompression is wholly
unjustified), and the same is true of many other media and binary file types
which embed some form of compression. The RAM Cache does detect compression
level and will declare something *incompressible* if it doesn't get below 90% of
the original size. This value is cached so that the RAM Cache will not attempt
to compress it again (at least as long as it is in the history).

