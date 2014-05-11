Example
*******

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

In the example below, suppose there is a cache hit and the cache returns
a vconnection that enables you to read the document from cache. To do
this, you need to prepare a buffer (``cache_bufp``) to hold the
document; meanwhile, use ``TSVConnCachedObjectSizeGet`` to find out the
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
        TSVConnCachedObjectSizeGet (cache_vconnp, &content_length);
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
