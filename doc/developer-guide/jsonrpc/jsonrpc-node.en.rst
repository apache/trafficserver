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

.. include:: ../../common.defs

.. _JSON: https://www.json.org/json-en.html
.. _JSONRPC: https://www.jsonrpc.org/specification
.. |RPC| replace:: JSONRPC 2.0


.. _jsonrpc-node:

JSONRPC Endpoint
****************

Traffic Server API clients can use different languages to connect and interact with the |RPC| node directly.
The goal of this section is to provide some tips on how to work with it.
To begin with, you should be familiar with the |RPC| protocol, you can check here :ref:`jsonrpc-protocol` and also `JSONRPC`_ .


IPC Node
========

You can directly connect to the Unix Domain Socket used for the |RPC| node, the location of the sockets
will depend purely on how did you configure the server, please check :ref:`admnin-jsonrpc-configuration` for
information regarding configuration.


Socket connectivity
-------------------

|RPC| server will close the connection once the server processed the incoming requests, so clients should be aware
of this and if sending multiple requests they should reconnect to the node once the response arrives. The protocol
allows you to send a bunch of requests together, this is called batch messages so it's recommended to send them all instead
of having a connection open and sending requests one by one. This being a local socket opening and closing the connection should
not be a big concern.



Using traffic_ctl
-----------------

:program:`traffic_ctl` can also be used to directly send raw |RPC| messages to the server's node, :program:`traffic_ctl` provides
several options to achieve this, please check ``traffic_ctl_rpc``.

