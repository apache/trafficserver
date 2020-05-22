.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSHttpTxnCacheLookupUrlGet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnCacheLookupUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

Description
===========

Get the current cache key URL, also referred to as the lookup URL. This must
be stored in a properly allocated URL object, typically created with a
:c:func:`TSUrlCreate()`.

TSHttpTxnCacheLookupUrlSet
==========================

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. c:function:: TSReturnCode TSHttpTxnCacheLookupUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

Description
-----------

Set the current cache key URL, also referred to as the lookup URL. This must
be stored in a properly allocated URL object, typically created with a
:c:func:`TSUrlCreate()` or :c:func:`TSUrlClone()`.

This API can be called as early as ``TS_HTTP_READ_REQUEST_HDR_HOOK`` but no later than
``TS_HTTP_POST_REMAP_HOOK``. This is the preferred and most efficient way to
modify the cache key, but an alternative is to use the old
:c:func:`TSCacheUrlSet()`, which takes a simple string as argument.

