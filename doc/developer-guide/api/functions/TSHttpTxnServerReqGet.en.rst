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

TSHttpTxnServerReqGet
*********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnServerReqGet(TSHttpTxn txnp, TSMBuffer * bufp, TSMLoc * obj)

Description
===========

Get the request |TS| is sending to the upstream (server) for the transaction :arg:`txnp`.
:arg:`bufp` and :arg:`obj` should be valid pointers to use as return values. The call site could
look something like ::

   TSMBuffer mbuffer;
   TSMLoc mloc;
   if (TS_SUCCESS == TSHttpTxnServerReqGet(&mbuffer, &mloc)) {
      /* Can use safely mbuffer, mloc for subsequent API calls */
   } else {
      /* mbuffer, mloc in an undefined state */
   }

This call returns :c:macro:`TS_SUCCESS` on success, and :c:macro:`TS_ERROR` on failure. It is the
caller's responsibility to see that :arg:`txnp` is a valid transaction.

Once the request object is obtained, it can be used to access all of the elements of the request,
such as the URL, the header fields, etc. This is also the mechanism by which a plugin can change the
upstream request, if done before the request is sent (in or before
:c:macro:`TS_HTTP_SEND_REQUEST_HDR_HOOK`). Note that for earlier hooks, the request may not yet
exist, in which case an error is returned.
