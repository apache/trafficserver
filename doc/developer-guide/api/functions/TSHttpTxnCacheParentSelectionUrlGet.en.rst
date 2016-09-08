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

.. default-domain:: c

TSHttpTxnCacheParentSelectionUrlSet
***********************************

Traffic Server Parent Selection consistent hash URL manipulation API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnCacheParentSelectionUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)
.. function:: TSReturnCode TSHttpTxnCacheParentSelectionUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

Description
===========

The Parent Selection consistent hash feature selects among multiple
parent caches based on hashing a URL (the HTTP request header URL).

These API functions allow an over-ride URL to be defined such that the
over-ride URL is hashed instead of the normal URL. In addition, the
various filtering options that may be applied to the normal URL
(fname, maxdirs, and qstring) are NOT applied when the over-ride is
used since it is assumed that custom filtering has already been
performed prior to explicitly setting the over-ride URL

:func:`TSHttpTxnCacheParentSelectionUrlSet` will set the over-ride URL.  

:func:`TSHttpTxnCacheParentSelectionUrlGet` will get the over-ride URL.

Return Values
=============

All these APIs returns a :type:`TSReturnCode`, indicating success
(:data:`TS_SUCCESS`) or failure (:data:`TS_ERROR`) of the operation.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUrlCreate(3ts)`,
:manpage:`TSUrlStringGet(3ts)`
