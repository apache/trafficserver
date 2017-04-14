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

.. _developer-cache-tiered-storage:

Tiered Storage
**************

Tiered storage is an attempt to allow |TS| to take advantage of physical storage
with different properties. This design concerns only mechanism. Policies to take
advantage of these are outside of the scope of this document. Instead we will
presume an *oracle* which implements this policy and describe the queries that
must be answered by the oracle and the effects of the answers.

Beyond avoiding question of tier policy, the design is also intended to be
effectively identical to current operations for the case where there is only
one tier.

The most common case for tiers is an ordered list of tiers, where higher tiers
are presumed faster but more expensive (or more limited in capacity). This is
not required. It might be that different tiers are differentiated by other
properties (such as expected persistence). The design here is intended to
handle both cases.

The design presumes that if a user has multiple tiers of storage and an ordering
for those tiers, they will usually want content stored at one tier level to also
be stored at every other lower level as well, so that it does not have to be
copied if evicted from a higher tier.

Configuration
=============

Each :term:`storage unit` in :file:`storage.config` can be marked with a
*quality* value which is 32 bit number. Storage units that are not marked are
all assigned the same value which is guaranteed to be distinct from all explicit
values. The quality value is arbitrary from the point of view of this design,
serving as a tag rather than a numeric value. The user (via the oracle) can
impose what ever additional meaning is useful on this value (rating, bit
slicing, etc.).

In such cases, all :term:`volumes <cache volume>` should be explicitly assigned
a value, as the default (unmarked) value is not guaranteed to have any
relationship to explicit values. The unmarked value is intended to be useful in
situations where the user has no interest in tiered storage and so wants to let
|TS| automatically handle all volumes as a single tier.

Operations
==========

After a client request is received and processed, volume assignment is done. For
each tier, the oracle would return one of four values along with a volume
pointer:

``READ``
    The tier appears to have the object and can serve it.

``WRITE``
    The object is not in this tier and should be written to this tier if
    possible.

``RW``
    Treat as ``READ`` if possible, but if the object turns out to not in the
    cache treat as ``WRITE``.

``NO_SALE``
    Do not interact with this tier for this object.

The :term:`volume <cache volume>` returned for the tier must be a volume with
the corresponding tier quality value. In effect, the current style of volume
assignment is done for each tier, by assigning one volume out of all of the
volumes of the same quality and returning one of ``RW`` or ``WRITE``, depending
on whether the initial volume directory lookup succeeds. Note that as with
current volume assignment, it is presumed this can be done from in memory
structures (no disk I/O required).

If the oracle returns ``READ`` or ``RW`` for more than one tier, it must also
return an ordering for those tiers (it may return an ordering for all tiers,
ones that are not readable will be ignored). For each tier, in that order, a
read of cache storage is attempted for the object. A successful read locks that
tier as the provider of cached content. If no tier has a successful read, or no
tier is marked ``READ`` or ``RW`` then it is a cache miss. Any tier marked
``RW`` that fails the read test is demoted to ``WRITE``.

If the object is cached, every tier that returns ``WRITE`` receives the object
to store in the selected volume (this includes ``RW`` returns that are demoted
to ``WRITE``). This is a cache to cache copy, not from the :term:`origin server`.
In this case, tiers marked ``RW`` that are not tested for read will not receive
any data and will not be further involved in the request processing.

For a cache miss, all tiers marked ``WRITE`` will receive data from the origin
server connection (if successful).

This means, among other things, that if there is a tier with the object all
other tiers that are written will get a local copy of the object, and the origin
server will not be used. In terms of implementation, currently a cache write to
a volume is done via the construction of an instance of :cpp:class:`CacheVC`
which recieves the object stream. For tiered storage, the same thing is done
for each target volume.

For cache volume overrides (via :file:`hosting.config`) this same process is
used except with only the volumes stripes contained within the specified cache
volume.

Copying
=======

It may be necessary to provide a mechanism to copy objects between tiers outside
of a client originated transaction. In terms of implementation, this is straight
forward using :cpp:class:`HttpTunnel` as if in a transaction, only using a
:cpp:class:`CacheVC` instance for both the producer and consumer. The more
difficult question is what event would trigger a possible copy. A signal could
be provided whenever a volume directory entry is deleted, although it should be
noted that the object in question may have already been evicted when this event
happens.

Additional Notes
================

As an example use, it would be possible to have only one cache volume that uses
tiered storage for a particular set of domains using volume tagging.
:file:`hosting.config` would be used to direct those domains to the selected
cache volume. The oracle would check the URL in parallel and return ``NO_SALE``
for the tiers in the target cache volume for other domains. For the other tier
(that of the unmarked storage units), the oracle would return ``RW`` for the
tier in all cases as that tier would not be queried for the target domains.

