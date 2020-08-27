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

.. include:: ../../../common.defs

.. _developer-plugins-io-cache-api:

Cache API
*********

.. toctree::
   :maxdepth: 2

The cache API enables plugins to read, write, and remove objects in the
Traffic Server cache. All cache APIs are keyed by an object called an
``TSCacheKey``; cache keys are created via ``TSCacheKeyCreate``; keys
are destroyed via ``TSCacheKeyDestroy``. Use ``TSCacheKeyDigestSet`` to
set the hash of the cache key.

Note that the cache APIs differentiate between HTTP data and plugin
data. The cache APIs do not allow you to write HTTP docs in the cache;
you can only write plugin-specific data (a specific type of data that
differs from the HTTP type).

**Example:**

.. code-block:: c

        const unsigned char *key_name = "example key name";

        TSCacheKey key;
        TSCacheKeyCreate (&key);
        TSCacheKeyDigestSet (key, (unsigned char *) key_name , strlen(key_name));
        TSCacheKeyDestroy (key);

Cache Reads
===========

``TSCacheRead`` does not really read - it is used for lookups (see the
sample Protocol plugin). Possible callback events include:

-  ``TS_EVENT_CACHE_OPEN_READ`` - indicates the lookup was successful.
   The data passed back along with this event is a cache vconnection
   that can be used to initiate a read on this keyed data.

-  ``TS_EVENT_CACHE_OPEN_READ_FAILED`` - indicates the lookup was
   unsuccessful. Reasons for this event could be that another
   continuation is writing to that cache location, or the cache key
   doesn't refer to a cached resource. Data payload for this event
   indicates the possible reason the read failed (``TSCacheError``).


Cache Writes
============

Use ``TSCacheWrite`` to write to a cache (see the :ref:`sample Protocol
plugin <about-the-sample-protocol>`). Possible
callback events include:

-  ``TS_EVENT_CACHE_WRITE_READ`` - indicates the lookup was successful.
   The data passed back along with this event is a cache vconnection
   that can be used to initiate a cache write.

-  ``TS_EVENT_CACHE_OPEN_WRITE_FAILED`` - event returned when another
   continuation is currently writing to this location in the cache. Data
   payload for this event indicates the possible reason for the write
   failing (``TSCacheError``).


Cache Removes
=============

Use ``TSCacheRemove`` to remove items from the cache. Possible callback
events include:

-  ``TS_EVENT_CACHE_REMOVE`` - the item was removed. There is no data
   payload for this event.

-  ``TS_EVENT_CACHE_REMOVE_FAILED`` - indicates the cache was unable to
   remove the item identified by the cache key. ``TSCacheError`` data
   indicates why the remove failed.


Errors
======

Errors pertaining to the failure of various cache operations are
indicated by ``TSCacheError`` (enumeration). They are as follows:

-  ``TS_CACHE_ERROR_NO_DOC`` - the key does not match a cached resource

-  ``TS_CACHE_ERROR_DOC_BUSY`` - e.g, another continuation could be
   writing to the cache location

-  ``TS_CACHE_ERROR_NOT_READY`` - the cache is not ready


Example
=======

In the example below, suppose there is a cache hit and the cache returns
a vconnection that enables you to read the document from cache. To do
this, you need to prepare a buffer (``cache_bufp``) to hold the
document; meanwhile, use ``TSVConnCacheObjectSizeGet`` to find out the
actual size of the document (``content_length``). Then, issue
``TSVConnRead`` to read the document with the total data length required
as ``content_length``. Assume the following data:

.. code-block:: c

        TSIOBuffer       cache_bufp = TSIOBufferCreate ();
        TSIOBufferReader cache_readerp = TSIOBufferReaderAlloc (out_bufp);
        TSVConn          cache_vconnp = NULL;
        TSVIO            cache_vio = NULL;
        int               content_length = 0;

In the ``TS_CACHE_OPEN_READ`` handler:

.. code-block:: c

    cache_vconnp = (TSVConn) data;
        content_length = TSVConnCacheObjectSizeGet (cache_vconnp);
        cache_vio = TSVConnRead (cache_vconn, contp, cache_bufp, content_length);

In the ``TS_EVENT_VCONN_READ_READY`` handler:

.. code-block:: c

    (usual VCONN_READ_READY handler logic)
    int nbytes = TSVIONBytesGet (cache_vio);
    int ntodo  = TSVIONTodoGet (cache_vio);
    int ndone  = TSVIONDoneGet (cache_vio);
    (consume data in cache_bufp)
    TSVIOReenable (cache_vio);

Do not try to get continuations or VIOs from ``TSVConn`` objects for
cache vconnections. Also note that the following APIs can only be used
on transformation vconnections and must not be used on cache
vconnections or net vconnections:

-  ``TSVConnWriteVIOGet``

-  ``TSVConnReadVIOGet``

-  ``TSVConnClosedGet``

APIs such as ``TSVConnRead``, ``TSVConnWrite``, ``TSVConnClose``,
``TSVConnAbort``, and ``TSVConnShutdown`` can be used on any kind of
vconnections.

When you are finished:

``TSCacheKeyDestroy (key);``
