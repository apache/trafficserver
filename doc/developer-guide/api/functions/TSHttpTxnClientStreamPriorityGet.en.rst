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

TSHttpTxnClientStreamPriorityGet
********************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnClientStreamPriorityGet(TSHttpTxn txnp, TSHttpPriority* priority)

Description
===========

Retrieve the priority information for the HTTP stream associated with the
provided transaction.  The resultant priority information is populated in the
``priority`` output variable. The ``TSHttpPriority`` type is designed to be
agnostic of the various HTTP protocol versions that support HTTP streams. The
user should pass a pointer casted to ``TSHttpPriority`` from a previously
allocated ``TSHttp2Priority`` structure. This design anticipates future support
for HTTP versions that support streams, such as HTTP/3.

The ``TSHttp2Priority`` structure has the following declaration:

.. code-block:: cpp

  typedef struct {
    uint8_t priority_type; /** HTTP_PROTOCOL_TYPE_HTTP_2 */
    int32_t stream_dependency;
    uint8_t weight;
  } TSHttp2Priority;

In a call to ``TSHttpTxnClientStreamPriorityGet``, the dependency and weight
will be populated in the ``stream_dependency`` and ``weight`` members,
respectively.  If the stream associated with the given transaction has no
dependency, then the ``stream_dependency`` output parameter will be populated
with ``-1`` and the value of ``weight`` will be meaningless. See RFC 7540
section 5.3 for details concerning HTTP/2 stream priority.

This API returns an error if the provided transaction is not an HTTP/2
transaction.

See Also
========

:doc:`TSHttpTxnClientStreamIdGet.en`
