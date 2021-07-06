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

TSHttpTxnClientStreamIdGet
**************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnClientStreamIdGet(TSHttpTxn txnp, uint64_t* stream_id)

Description
===========

Retrieve the stream identification for the HTTP stream of which the provided
transaction is a part. The resultant stream identifier is populated in the
``stream_id`` output parameter.

This interface currently only supports HTTP/2 streams. See RFC 7540 section
5.1.1 for details concerning HTTP/2 stream identifiers.

This API returns an error if the provided transaction is not an HTTP/2
transaction.

See Also
========

:doc:`TSHttpTxnClientStreamPriorityGet.en`
