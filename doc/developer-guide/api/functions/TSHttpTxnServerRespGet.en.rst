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

TSHttpTxnServerRespGet
**********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnServerRespGet(TSHttpTxn txnp, TSMBuffer * bufp, TSMLoc * offset)

Description
===========

Get the response header sent by the upstream server. This will only be useful in a callback on a
hook that is called after the upstream responds, and if there was an upstream response. For
instance, if the inbound request has no remap rule and :ts:cv:`remap is required
<proxy.config.url_remap.remap_required>` then there will be no server response because no outbound
connection was made. In this case the function will return :c:macro:`TS_ERROR`.

The response header is returned in :arg:`bufp` and :arg:`offset`. :arg:`bufp` is the heap in which
the header resides, and :arg:`offset` is the location in that heap. These will be used in subsequent
calls to retrieve information from the header.

.. code-block:: cpp
   :emphasize-lines: 4

   int get_response_status(TSHttpTxn txn) {
      TSMBuffer resp_heap = nullptr;
      TSMLoc resp_hdr = nullptr;
      if (TS_SUCCESS == TSHttpTxnServerRespGet(tnx, &resp_heap, &resp_hdr)) {
         return TSHttpHdrStatusGet(resp_headp, resp_hdr);
      }
      return HTTP_STATUS_NONE;
   }
