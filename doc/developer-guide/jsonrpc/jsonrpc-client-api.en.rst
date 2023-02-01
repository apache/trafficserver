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

.. |RPC| replace:: JSONRPC 2.0

.. _YAML: https://github.com/jbeder/yaml-cpp/wiki/Tutorial

.. _developer-guide-jsonrpc-client-api:

Traffic Server JSONRPC Node C++ Client Implementation
*****************************************************

Basics
======

|TS| provides a set of basic C++ classes to perform request and handle responses from
server's rpc node.
Files under `include/shared/rpc` are meant to be used by client applications like :program:`traffic_ctl`
and :program:`traffic_top` to send a receive messages from the |TS| jsonrpc node.

This helper classes provides:

  * ``RPCClient`` class which provides functionality to connect and invoke remote command inside the |TS| |RPC| node.
    This class already knows where the unix socket is located.

  * ``IPCSocketClient`` class which provides the socket implementation for the IPC socket in the |RPC| node. If what
    you want is just to invoke a remote function and get the response, then it's recommended to just use the ``RPCClient``
    class instead.

  * ``RPCRequests`` class which contains all the basic classes to map the basic |RPC| messages(requests and responses).
    If what you want is a custom message you can just subclass ``shared::rpc::ClientRequest`` and override the ``get_method()``
    member function. You can check ``CtrlRPCRequests.h`` for examples.
    The basic encoding and decoding for these structures are already implemented inside ``yaml_codecs.h``. In case you define
    your own message then you should provide you own codec implementation, there are some examples available in ``ctrl_yaml_codecs.h``

Building
========

..
  _ TBC.
