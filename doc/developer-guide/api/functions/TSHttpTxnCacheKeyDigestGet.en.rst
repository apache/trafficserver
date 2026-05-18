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

.. default-domain:: cpp

TSHttpTxnCacheKeyDigestGet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnCacheKeyDigestGet(TSHttpTxn txnp, char *buffer, int *length)

Description
===========

Get the effective cache key digest (cryptographic hash) that was used for
cache lookup or storage on this transaction. This is the raw hash bytes,
not a hex or base64 encoding.

The digest size depends on the build configuration: 16 bytes for MD5
(default) or 32 bytes for SHA-256 (FIPS mode). A 32-byte buffer is
sufficient for either mode:

.. code-block:: c

    char digest[32];
    int  digest_len = sizeof(digest);
    if (TSHttpTxnCacheKeyDigestGet(txnp, digest, &digest_len) == TS_SUCCESS) {
      // digest_len contains the actual number of bytes written
    }

Pass :code:`nullptr` for *buffer* to query the digest size without
copying.

Returns :enumerator:`TS_SUCCESS` if a cache key was computed for the
transaction. Returns :enumerator:`TS_ERROR` if no cache lookup was
performed, or if *buffer* is non-null and *\*length* is smaller than the
digest size. In all cases *\*length* is set to the required digest size
on return.

See Also
========

:func:`TSHttpTxnCacheLookupUrlGet`
