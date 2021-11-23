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

TSHttpTxnPostBufferReaderGet
****************************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSIOBufferReader TSHttpTxnPostBufferReaderGet(TSHttpTxn txnp)

Description
===========

Retrieve the client request body for the transaction referenced by :arg:`txnp`.
The body is read from via the returned :c:type:`TSIOBufferReader`. The returned
:c:type:`TSIOBufferReader` is owned by the caller and the caller must free it
via :c:func:`TSIOBufferReaderFree`. This function should be used in the handler
for :data:`TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK`. The following example
handler makes use of :c:func:`TSHttpTxnPostBufferReaderGet`.

.. code-block:: c

   int
   CB_Read_Request_Body_Hook(TSCont contp, TSEvent event, void* data) {
     ink_assert(event == TS_EVENT_HTTP_REQUEST_BUFFER_COMPLETE);
     auto txnp = reinterpret_cast<TSHttpTxn>(data);

     TSIOBufferReader post_buffer_reader = TSHttpTxnPostBufferReaderGet(txnp);
     int64_t read_avail                  = TSIOBufferReaderAvail(post_buffer_reader);

     if (read_avail > 0) {
       char *body_bytes = reinterpret_cast<char *>(TSmalloc(sizeof(char) * read_avail));

       int64_t consumed      = 0;
       TSIOBufferBlock block = TSIOBufferReaderStart(post_buffer_reader);
       while (block != NULL) {
         int64_t data_len        = 0;
         const char *block_bytes = TSIOBufferBlockReadStart(block, post_buffer_reader, &data_len);
         memcpy(body_bytes + consumed, block_bytes, data_len);
         consumed += data_len;
         block = TSIOBufferBlockNext(block);
       }

       // Now do something with body_bytes which contains the contents of the
       // request's body.
     }

     // Remember to free the TSIOBufferReader.
     TSIOBufferReaderFree(post_buffer_reader);

     TSHttpTxnReenable(txnp);
     return 0;
   }

:ts:git:`example/plugins/c-api/request_buffer/request_buffer.c` is a simple
yet complete plugin that accesses HTTP request bodies.
