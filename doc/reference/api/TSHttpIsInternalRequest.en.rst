.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at
   
       http://www.apache.org/licenses/LICENSE-2.0
   
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. default-domain:: c

=======================
TSHttpIsInternalRequest
=======================

Test whether a request is internally-generated.

Synopsis
========
`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpIsInternalRequest(TSHttpTxn txnp)
.. function:: TSReturnCode TSHttpIsInternalSession(TSHttpSsn ssnp)

Description
===========

:func:`TSHttpIsInternalRequest` tests whether a HTTP transaction
was originated within Traffic Server.

:func:`TSHttpIsInternalSession` tests whether a HTTP session
was originated within Traffic Server.

Return Values
=============

Both these APIs returns a :type:`TSReturnCode`, indicating whether the
object was internal (:data:`TS_SUCCESS`) or not (:data:`TS_ERROR`).

Examples
========

The ESI plugin uses :func:`TSHttpIsInternalRequest` to ignore requests that is
had generated while fetching portions of an ESI document:

.. literalinclude:: ../../../plugins/experimental/esi/esi.cc
  :language: c
  :lines: 1395-1398

See also
========
:manpage:`TSAPI(3ts)`
