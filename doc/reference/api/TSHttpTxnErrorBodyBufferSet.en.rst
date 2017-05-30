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

.. default-domain:: c

TSHttpTxnErrorBodyBufferSet
===========================

Sets an error type body to a transaction.


Synopsis
--------

`#include <ts/ts.h>`

.. function:: void TSHttpTxnErrorBodyBufferSet(TSHttpTxn txnp, TSIOBufferReader body, char const* mimetype)


Description
-----------

This sets the body for the response to the user agent when the status is an error code.

:arg:`mimetype` must be allocated with :func:`TSmalloc` or :func:`TSstrdup`.
The mimetype argument is optional, if not provided it defaults to "text/html".  Sending an emptry
string would prevent setting a content type header (but that is not advised).
