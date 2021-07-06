.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../../../common.defs

.. _developer-plugins-denylist-access-process-txn:

Accessing the Transaction Being Processed
*****************************************

A continuation's handler function is of type ``TSEventFunc``; the
prototype is as follows:

``static int function_name (TSCont contp, TSEvent event, void *edata)``

In general, the return value of the handler function is not used. The
continuation argument is the continuation being called back, the event
is the event being sent to the continuation, and the data pointed to by
``void *edata`` depends on the type of event. The data types for each
event type are listed in :doc:`Writing Handler
Functions <../../continuations/writing-handler-functions.en>`

The key here is that if the event is an HTTP transaction event, then the
data passed to the continuation's handler is of type ``TSHttpTxn`` (a
data type that represents HTTP transactions). Your plugin can then do
things with the transaction. Here's how it looks in the code for the
Denylist plugin's handler:

.. code-block:: c

   static int
   denylist_plugin (TSCont contp, TSEvent event, void *edata)
   {
      TSHttpTxn txnp = (TSHttpTxn) edata;
      switch (event) {
         case TS_EVENT_HTTP_OS_DNS:
            handle_dns (txnp, contp);
            return 0;
         case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
            handle_response (txnp);
            return 0;
         default:
            break;
      }
      return 0;
   }

For example: when the origin server DNS lookup event is sent,
``denylist_plugin`` can call ``handle_dns``\ and pass ``txnp`` as an
argument.
