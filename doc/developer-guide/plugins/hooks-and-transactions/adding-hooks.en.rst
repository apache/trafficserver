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

.. _developer-plugins-hooks-adding:

Adding Hooks
************

There are several ways to add hooks to your plugin.

-  **Global HTTP hooks** HTTP transaction hooks are set on a global
   basis using the function ``TSHttpHookAdd``. This means that the
   continuation specified as the parameter to ``TSHttpHookAdd`` is
   called for every transaction. ``TSHttpHookAdd`` must be used in
   ``TSPluginInit``.

-  **Transaction hooks** Transaction hooks can be used to call plugins
   back for a specific HTTP transaction. You cannot add transaction
   hooks in ``TSPluginInit``; you first need a handle to a transaction.
   See :ref:`developer-plugins-blacklist-access-process-txn`.

-  **Transformation hooks** Transformation hooks are a special case of
   transaction hooks. See
   :c:func:`TSVConnCacheObjectSizeGet`
   for more information about transformation hooks. You add a
   transformation hook using ``TSHttpTxnHookAdd``, as described in
   :ref:`developer-plugins-hooks-http-transactions`.

-  **Session hooks** An HTTP session starts when a client opens a
   connection to Traffic Server and ends when the connection closes. A
   session can consist of several transactions. Session hooks enable you
   to hook your plugin to a particular point in every transaction within
   a specified session (see :doc:`HTTP Sessions <http-sessions.en>`).
   Session hooks are added in a manner similar to transaction hooks (ie,
   you first need a handle to an HTTP session).

-  **HTTP select alternate hook** Alternate selection hooks enable you
   to hook on to the alternate selection state. These hooks must be
   added globally, since Traffic Server does not have a handle to a
   transaction or session when alternate selection is taking place. See
   :doc:`HTTP Alternate Selection <http-alternate-selection.en>` for
   information on the alternate selection mechanism.

All of the hook addition functions
(:c:func:`TSHttpHookAdd`,
:c:func:`TSHttpSsnHookAdd`,
:c:func:`TSHttpSsnReenable`)
take ``TSHttpHookID`` (identifies the hook to add on to) and ``TSCont``
(the basic callback mechanism in Traffic Server). A single ``TSCont``
can be added to any number of hooks at a time.

An HTTP hook is identified by the enumerated type ``TSHttpHookID``. The
values for ``TSHttpHookID`` are:

**Values for TSHttpHookID**

``TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK``
    Called after the HTTP state machine has completed the cache lookup
    for the document requested in the ongoing transaction. Register this
    hook via ``TSHttpTxnHookAdd`` or ``TSHttpHookAdd``. Corresponds to
    the event ``TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE``.

``TS_HTTP_OS_DNS_HOOK``
    Called immediately after the HTTP state machine has completed a DNS
    lookup of the origin server. The HTTP state machine will know the
    origin server's IP address at this point, which is useful for
    performing both authentication and blacklisting. Corresponds to the
    event ``TS_EVENT_HTTP_OS_DNS``.

``TS_HTTP_POST_REMAP_HOOK``
    Called immediately after remapping occurs, before cache lookup.
    Corresponds to the event ``TS_EVENT_HTTP_POST_REMAP``.

``TS_HTTP_PRE_REMAP_HOOK``
    Called after the request header is read from the client, before any
    remapping of the headers occurs. Corresponds to the event
    ``TS_EVENT_HTTP_PRE_REMAP``.

``TS_HTTP_READ_CACHE_HDR_HOOK``
    Called immediately after the request and response header of a
    previously-cached object is read from cache. This hook is only
    called if the document is being served from cache. Corresponds to
    the event ``TS_EVENT_HTTP_READ_CACHE_HDR``.

``TS_HTTP_READ_RESPONSE_HDR_HOOK``
    Called immediately after the response header is read from the origin
    server or parent proxy. Corresponds to the event
    ``TS_EVENT_HTTP_READ_RESPONSE_HDR``.

``TS_HTTP_RESPONSE_TRANSFORM_HOOK``
    See :ref:`"Transformations" <transformations>`
    for information about transformation hooks.

``TS_HTTP_READ_REQUEST_HDR_HOOK``
    Called immediately after the request header is read from the client.
    Corresponds to the event ``TS_EVENT_HTTP_READ_REQUEST_HDR``.

``TS_HTTP_REQUEST_TRANSFORM_HOOK``
    See :ref:`"Transformations" <transformations>`
    for information about transformation hooks.

``TS_HTTP_SELECT_ALT_HOOK``
    See :doc:`"HTTP Alternate Selection" <http-alternate-selection.en>` for
    information about the alternate selection mechanism.

``TS_HTTP_SEND_RESPONSE_HDR_HOOK``
    Called immediately before the proxy's response header is written to
    the client; this hook is usually used for modifying the response
    header. Corresponds to the event
    ``TS_EVENT_HTTP_SEND_RESPONSE_HDR``.

``TS_HTTP_SEND_REQUEST_HDR_HOOK``
    Called immediately before the proxy's request header is sent to the
    origin server or the parent proxy. This hook is not called if the
    document is being served from cache. This hook is usually used for
    modifying the proxy's request header before it is sent to the origin
    server or parent proxy.

.. caution::

   TS_HTTP_SEND_REQUEST_HDR_HOOK may callback several times when the  
   OS crashed. Be careful to use functions such as TSContDestroy in 
   TS_HTTP_SEND_REQUEST_HDR_HOOK hook.

``TS_HTTP_SSN_CLOSE_HOOK``
    Called when an HTTP session ends. A session ends when the client
    connection is closed. You can only add this hook as a global hook

``TS_HTTP_SSN_START_HOOK``
    Called when an HTTP session is started. A session starts when a
    client connects to Traffic Server. You can only add this hook as a
    global hook.

``TS_HTTP_TXN_CLOSE_HOOK``
    Called when an HTTP transaction ends.

``TS_HTTP_TXN_START_HOOK``
    Called when an HTTP transaction is started. A transaction starts
    when either a client connects to Traffic Server and data is
    available on the connection, or a previous client connection that
    was left open for keep alive has new data available.

The function you use to add a global HTTP hook is
:c:func:`TSHttpHookAdd`.

