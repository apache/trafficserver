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

.. include:: common.defs

===========================
Cache Related API functions
===========================

.. c:function:: void TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag)

   Set a flag that marks a request as cacheable. This is a positive override
   only, setting :c:arg:``flag`` to ``0`` restores the default behavior, it does not
   force the request to be uncacheable.

.. c:function:: TSReturnCode TSCacheUrlSet(TSHttpTxn txnp, char const* url, int length)

   Set the cache key for the transaction :c:arg:``txnp`` as the string pointed at by
   :c:arg:``url`` of :c:arg:``length`` characters. It need not be NUL-terminated. This should
   be called from ``TS_HTTP_READ_REQUEST_HDR_HOOK`` which is before cache lookup
   but late enough that the HTTP request header is available.

===============
Cache Internals
===============

.. cpp:function:: int DIR_SIZE_WITH_BLOCK(int big)

   A preprocessor macro which computes the maximum size of a fragment based on
   the value of :cpp:arg:``big``. This is computed as if the argument where the value of
   the :cpp:arg:``big`` field in a struct :cpp:class:`Dir`.

.. cpp:function:: int DIR_BLOCK_SIZE(int big)

   A preprocessor macro which computes the block size multiplier for a struct
   :cpp:class:`Dir` where :cpp:arg:``big`` is the :cpp:arg:``big`` field value.
