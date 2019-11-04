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

TSHttpTxnParentSelectionUrlSet
***********************************

Traffic Server Parent Selection consistent hash URL manipulation API.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnParentSelectionUrlSet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)
.. function:: TSReturnCode TSHttpTxnParentSelectionUrlGet(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc offset)

Description
===========

The Parent Selection consistent hash feature selects among multiple
parent caches based on hashing a URL (the HTTP request header URL).

These API functions allow an over-ride URL to be defined such that the
over-ride URL is hashed instead of the normal (header request) URL. In
addition, any filtering options that may be applied to the normal URL
(such as qstring) are NOT applied to the over-ride URL since it is
assumed that custom filtering has already been performed prior to
explicitly setting the over-ride URL.

Note that the normal URL is only hashed on the path and query string
portion (optionally excluded with the qstring option). However, the
over-ride URL is hashed on the entire URL string as returned by
URL::string_get_ref(). This includes the scheme and hostname such as
"http://hostname" which occur prior to the path.

If the non-path URL elements should not be hashed in a meaningful
manner, then they should be normalized to some value (if they are
required in a valid URL) or excluded (if they are optional) when
generating the over-ride URL. For example, since the over-ride URL is
arbitrary, the URL scheme and hostname can simply be set to
"fake://fake.fake" when creating the over-ride URL.

:func:`TSHttpTxnParentSelectionUrlSet` will set the over-ride URL.

:func:`TSHttpTxnParentSelectionUrlGet` will get the over-ride URL.

Return Values
=============

All these APIs returns a :type:`TSReturnCode`, indicating success
(:data:`TS_SUCCESS`) or failure (:data:`TS_ERROR`) of the operation.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUrlCreate(3ts)`,
:manpage:`TSUrlStringGet(3ts)`
