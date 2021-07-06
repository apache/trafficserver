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

TSHttpTxnClientReqGet
*********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnClientReqGet(TSHttpTxn txnp, TSMBuffer * bufp, TSMLoc * offset)

Description
===========

Get the client request. The :arg:`txnp` must be passed in and the values in :arg:`bufp` and
:arg:`offset` are updated to refer to the client request in the transaction. A typical use case
would look like ::

   int
   CB_Read_Req_Hdr_Hook(TSCont contp, TSEvent event, void* data) {
      auto txnp = reinterpret_cast<TSHttpTxn>(data);
      TSMBuffer creq_buff;
      TSMLoc creq_loc;
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &creq_buff, &creq_loc)) {
         // use the client request
      } // else values in creq_buff, creq_loc are garbage.
      TSHttpTxnReenable(txnp);
      return 0;
   }

The values placed in :arg:`bufp` and :arg:`offset` are stable for the transaction and need only be
retrieved once per transaction. Note these values are valid only if this function returns
``TS_SUCCESS``.
