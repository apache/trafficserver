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

.. _developer-cache-consistency:

Cache Tools
~~~~~~~~~~~

Tools and techniques for cache monitoring and inspection.

* :ref:`The cache inspector <inspecting-the-cache>`.

Topics to be done
~~~~~~~~~~~~~~~~~

* Resident alternates
* Object refresh

Cache Consistency
~~~~~~~~~~~~~~~~~

The cache is completely consistent, up to and including kicking the power cord
out, if the write buffer on consumer disk drives is disabled. You need to use::

  hdparm -W0

The cache validates that all the data for the document is available and will
silently mark a partial document as a miss on read. There is no gentle
shutdown for Traffic Server. You simply kill the process and the recovery code
(fsck) is run every time Traffic Server starts up.

On startup the two versions of the index are checked, and the last valid one is
read into memory. |TS| then moves forward from the last snapped write
cursor and reads all the fragments written to disk and updates the directory
(as in a log-based file system). It stops reading at the write before the last
valid write header it sees (as a write is not necessarily atomic because of
sector reordering). Then the new updated index is written to the invalid
version (in case of a crash during startup) and the system starts.

.. _volume tagging:

Volume Tagging
~~~~~~~~~~~~~~

Currently, :term:`cache volumes <cache volume>` are allocated somewhat
arbitrarily from storage elements. `This enhancement <https://issues.apache.org/jira/browse/TS-1728>`__
allows :file:`storage.config` to assign :term:`storage units <storage unit>` to
specific :term:`volumes <cache volume>` although the volumes must still be
listed in :file:`volume.config` in general and in particular to map domains to
specific volumes. A primary use case for this is to be able to map specific
types of content to different storage elements. This can be employed to have
different storage devices for various types of content (SSD vs. rotational).

Version Upgrade
---------------

It is currently the case that any change to the cache format will clear the
cache. This is an issue when upgrading the |TS| version and should be kept in mind.

.. _cache-key:

Controlling the cache key
-------------------------

The :term:`cache key` is by default the URL of the request. There are two
possible choices, the original (pristine) URL and the remapped URL. Which of
these is used is determined by the configuration value
:ts:cv:`proxy.config.url_remap.pristine_host_hdr`.

This is an ``INT`` value. If set to ``0`` (disabled) then the remapped URL is
used, and if it is not ``0`` (enabled) then the original URL is used. This
setting also controls the value of the ``HOST`` header that is placed in the
request sent to the :term:`origin server`, using the hostname from the original
URL if not ``0`` and the host name from the remapped URL if ``0``. It has no
other effects.

For caching, this setting is irrelevant if no remapping is done or there is a
one-to-one mapping between the original and remapped URLs.

It becomes significant if multiple original URLs are mapped to the same
remapped URL. If pristine headers are enabled, requests to different original
URLs will be stored as distinct :term:`objects <cache object>` in the cache. If
disabled, the remapped URL will be used and there may be collisions. This is
bad if the contents different, but quite useful if they are the same (as in
situations where the original URLs are just aliases for the same underlying
server resource).

This is also an issue if a remapping is changed because it is effectively a
time axis version of the previous case. If an original URL is remapped to a
different server address then the setting determines if existing cached objects
will be served for new requests (enabled) or not (disabled). Similarly, if the
original URL mapped to a particular URL is changed then cached objects from the
initial original URL will be served from the updated original URL if pristine
headers is disabled.

These collisions are not by themselves good or bad. An administrator needs to
decide which is appropriate for their situation and set the value correspondingly.

If a greater degree of control is desired, a plugin must be used to invoke the
API calls :c:func:`TSHttpTxnCacheLookupUrlSet()` or  :c:func:`TSCacheUrlSet()`
to provide a specific :term:`cache key`. The :c:func:`TSCacheUrlSet()` API can
be called as early as ``TS_HTTP_READ_REQUEST_HDR_HOOK`` but no later than
``TS_HTTP_POST_REMAP_HOOK``. It can be called only once per transaction;
calling it multiple times has no additional effect.

A plugin that changes the cache key must do so consistently for both cache hit
and cache miss requests because two different requests that map to the same
cache key will be considered equivalent by the cache. Use of the URL directly
provides this and so must any substitute. This is entirely the responsibility
of the plugin; there is no way for the |TS| core to detect such an occurrence.

If :c:func:`TSHttpTxnCacheLookupUrlGet()` is called after new cache url set by
:c:func:`TSHttpTxnCacheLookupUrlSet()` or :c:func:`TSCacheUrlSet()`, it should
use a URL location created by :c:func:`TSUrlCreate()` as its third input
parameter instead of getting ``url_loc`` from the client request.

It is a requirement that the string be syntactically a URL but otherwise it is
completely arbitrary and need not have any path. For instance, if the company
Network Geographics wanted to store certain content under its own
:term:`cache key`, using a document GUID as part of the key, it could use a
cache key like ::

   ngeo://W39WaGTPnvg

The scheme ``ngeo`` was picked specifically because it is not a valid URL
scheme, and so will never collide with any valid URL.

This can be useful if the URL encodes both important and unimportant data.
Instead of storing potentially identical content under different URLs (because
they differ on the unimportant parts) a url containing only the important parts
could be created and used.

For example, suppose the URL for Network Geographics content encoded both the
document GUID and a referral key. ::

   http://network-geographics-farm-1.com/doc/W39WaGTPnvg.2511635.UQB_zCc8B8H

We don't want to serve the same content for every possible referrer. Instead,
we could use a plugin to convert this to the previous example and requests that
differed only in the referrer key would all reference the same cache entry.
Note that we would also map the following to the same cache key ::

   http://network-geographics-farm-56.com/doc/W39WaGTPnvg.2511635.UQB_zCc8B8H

This can be handy for sharing content between servers when that content is
identical. Plugins can change the cache key, or not, depending on any data in
the request header. For instance, not changing the cache key if the request is
not in the ``doc`` directory. If distinguishing servers is important, that can
easily be pulled from the request URL and used in the synthetic cache key. The
implementer is free to extract all relevant elements for use in the cache key.

While there is no explicit requirement that the synthetic cache key be based on
the HTTP request header, in practice it is generally necessary due to the
consistency requirement. Because cache lookup happens before attempting to
connect to the :term:`origin server`, no data from the HTTP response header is
available, leaving only the request header. The most common case is the one
described above where the goal is to elide elements of the URL that do not
affect the content to minimize cache footprint and improve cache hit rates.
