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

.. _developer-cache-api-functions:

API functions
*************

.. c:function:: void TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag)

   Set a flag that marks a request as cacheable. This is a positive override
   only, setting :arg:`flag` to ``0`` restores the default behavior, it does not
   force the request to be uncacheable.

.. c:function:: TSReturnCode TSCacheUrlSet(TSHttpTxn txnp, char const* url, int length)

   Set the cache key for the transaction :arg:`txnp` as the string pointed at by
   :arg:`url` of :arg:`length` characters. It need not be NUL-terminated. This should
   be called from ``TS_HTTP_READ_REQUEST_HDR_HOOK`` which is before cache lookup
   but late enough that the HTTP request header is available.

.. c:function:: TSReturnCode TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

   Get the current cache key URL, also referred to as the lookup URL. This must
   be stored in a properly allocated URL object, typically created with a
   :c:func:`TSUrlCreate()`.

.. c:function:: TSReturnCode TSHttpTxnCacheLookupUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

   Set the current cache key URL, also referred to as the lookup URL. This must
   be stored in a properly allocated URL object, typically created with a
   :c:func:`TSUrlCreate()` or :c:func:`TSUrlClone()`.


The APIs that modify the cache key can be called as early as
``TS_HTTP_READ_REQUEST_HDR_HOOK`` but no later than
``TS_HTTP_POST_REMAP_HOOK``. The cache key is not only used for a cache lookup
before going to origin, but also to mark the intent to write to cache on an
origin response (if possible).

Cache Internals
===============

.. cpp:function:: int DIR_SIZE_WITH_BLOCK(int big)

   A preprocessor macro which computes the maximum size of a fragment based on
   the value of :arg:`big`. This is computed as if the argument where the value of
   the :arg:`big` field in a struct :cpp:class:`Dir`.

.. cpp:function:: int DIR_BLOCK_SIZE(int big)

   A preprocessor macro which computes the block size multiplier for a struct
   :cpp:class:`Dir` where :arg:`big` is the :arg:`big` field value.
