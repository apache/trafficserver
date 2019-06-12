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

TSHttpTxnIsWebsocket
********************

Test whether a request is attempting to initiate Websocket connection.

Synopsis
========

`#include <ts/ts.h>`

.. function:: int TSHttpTxnIsWebsocket(TSHttpTxn txnp)

Description
===========

:func:`TSHttpTxnIsWebsocket` tests whether the transaction
is a WebSocket upgrade request.

Return Values
=============

A non-zero value is returned if the relevant header value is found.

See also
========

:manpage:`TSAPI(3ts)`
