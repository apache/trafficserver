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

==============================
Tiered Storage Design
==============================

.. include:: common.defs

--------------
Introduction
--------------

Tiered storage is an attempt to allow |TS| to take advantage of physical storage with different properties. This design concerns only mechanism. Policies to take advantage of these are outside of the scope of this document. Instead we will presume an *oracle* which implements this policy and describe the queries that must be answered by the oracle and the effects of the answers.

Beyond avoiding question of tier policy the design is also intended to be effectively identical to current operations for the case where there is only one tier.

-------------
Configuration
-------------

Each storage unit in :file:`storage.config` can be marked with a *quality* value which is 32 bit number. Storage units that are not marked are all assigned the same value which is guaranteed to be distinct from all explicit values.

-------------
Operations
-------------

When a request is received from a client volume assignment is done in parallel for each tier quality. The oracle is queried for each tier and returns a volume and one of four values

`READ`
    The tier appears to have the object and can serve it.

`WRITE`
    The object is not in this tier and should be written to this tier if possible.

`RW`
    Treat as `READ` if possible but if the object turns out to not in the cache treat as `WRITE`.

`NO_SALE`
    Do not interact with this tier for this object.

The vvolume returned by the tier must be a volume with the corresponding quality value. In effect the current style of volume assignment is done for each tier, by assigning one volume out of all of the volumes of the same quality and returning one of `RW` or `WRITE` depending on whether the initial volume directory lookup succeeds.

If the object is cached every tier that returns `WRITE` receives the object to store in the selected volume (this includes `RW` returns that are demoted to `WRITE`). If there is more than one `READ` or `RW` the oracle is consoluted (or configures at start time) an ordering of the tiers. The tiers are tried in order and the first that successfully accesses the object becomes the provider. If no tier is successful then the object is retrieved from the origin server.

This means, among other things, that if there is a tier with the object all other tiers that are written will get a local copy of the object, the origin server will not be used. In terms of implementation, currently a cache write to a volume is done via the construction of an instance of `CacheVC` which recieves the object stream. For tiered storage the same thing is done for each target volume.

For cache volume overrides (e.g. via :file:`hosting.config`) this same process is used except with only the volumes stripes contained within the specified cache volume.

-------
Copying
-------

It may be necessary to provide a mechanism to copy objects between tiers outside of a client originated transaction. In terms of implementation this is straight forward using `HttpTunnel` as if in a transaction only using a `CacheVC` instance for both the producer and consumer. The more difficult question is what event would trigger a possible copy. A signal could be provided whenever a volume directory entry is deleted although it should be noted that the object in question may have already been evicted when this event happens.

----------------
Additional Notes
----------------

As an example use, it would be possible to have only one cache volume that uses tiered storage for a particular set of domains using volume tagging. :file:`hosting.config` would be used to direct those domains to the selected cache volume. The oracle would check the URL in parallel and return `NO_SALE` for the tiers in the target cache volume for other domains. For the other tier (that of the unmarked storage units) the oracle would return `RW` for the tier in all cases as that tier would not be queried for the target domains.
