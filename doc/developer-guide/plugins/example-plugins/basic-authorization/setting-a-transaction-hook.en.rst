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

Setting a Transaction Hook
**************************

If the request does not have the ``Proxy-Authorization`` field set to
Basic authorization or a valid username/password, then the plugin sends
the 407 Proxy authorization ``required`` status code back to the client.
The client will then prompt the user for a username and password, and
then resend the request.

In the ``handle_dns`` routine, the following lines handle the
authorization error case:

.. code-block:: c

    done:
         TSHttpTxnHookAdd (txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
         TSHttpTxnReenable (txnp, TS_EVENT_HTTP_ERROR);

If ``handle_dns`` does not find the ``Proxy-Authorization`` field set to
Basic authorization or a valid username/password, then it adds a
``SEND_RESPONSE_HDR_HOOK`` to the transaction being processed. This
means that Traffic Server will call the plugin back when sending the
client response. ``handle_dns`` reenables the transaction with
``TS_EVENT_HTTP_ERROR``, which means that the plugin wants Traffic
Server to terminate the transaction.

When Traffic Server terminates the transaction, it sends the client an
error message. Because of the ``SEND_RESPONSE_HDR_HOOK``, Traffic Server
calls the plugin back. The ``auth-plugin`` routine calls
``handle_response`` to send the client a ``407`` status code. When the
client resends the request with the ``Proxy-Authorization`` field, a new
transaction begins.

``handle_dns`` calls ``base64_decode`` to decode the username and
password; ``handle_dns`` also calls ``authorized`` to validate the
username and password. In this plugin, sample NT code is provided for
password validation. UNIX programmers can supply their own validation
mechanism.

