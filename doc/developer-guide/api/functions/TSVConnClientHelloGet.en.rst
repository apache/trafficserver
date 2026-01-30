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

TSVConnClientHelloGet
*********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSClientHello TSVConnClientHelloGet(TSVConn sslp)
.. function:: void TSClientHelloDestroy(TSClientHello ch)
.. function:: TSReturnCode TSClientHelloExtensionGet(TSClientHello ch, unsigned int type, const unsigned char **out, size_t *outlen)

Description
===========

:func:`TSVConnClientHelloGet` retrieves ClientHello message data from the TLS
virtual connection :arg:`sslp`. This function is typically called from the
``TS_EVENT_SSL_CLIENT_HELLO`` hook. Returns ``nullptr`` if
:arg:`sslp` is invalid or not a TLS connection.

The caller must call :func:`TSClientHelloDestroy` to free the returned object.

:func:`TSClientHelloDestroy` frees the :type:`TSClientHello` object :arg:`ch`.

:func:`TSClientHelloExtensionGet` retrieves extension data for the specified
:arg:`type` (e.g., ``0x10`` for ALPN). Returns :enumerator:`TS_SUCCESS` if
found, :enumerator:`TS_ERROR` otherwise. The returned pointer in :arg:`out` is
valid only while :arg:`ch` exists.
