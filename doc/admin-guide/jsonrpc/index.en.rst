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

.. highlight:: cpp
.. default-domain:: cpp

.. |RPC| replace:: JSONRPC 2.0

.. _admin-jsonrpc:

JSONRPC
*******

.. _admin-jsonrpc-description:

Description
===========

|TS| Implements and exposes management calls using a JSONRPC API.  This API is base on the following two things:

* `JSON  <https://www.json.org/json-en.html>`_  format. Lightweight data-interchange format. It is easy for humans to read and write.
  It is easy for machines to parse and generate. It's basically a  collection of name/value pairs.

* `JSONRPC 2.0 <https://www.jsonrpc.org/specification>`_ protocol. Stateless, light-weight remote procedure call (RPC) protocol.
  Primarily this specification defines several data structures and the rules around their processing.


In order for programs to communicate with |TS|, the server exposes a ``JSONRRPC 2.0`` API where programs can communicate with it.




.. _admnin-jsonrpc-configuration:

Configuration
=============

The |RPC| server can be configured using the following configuration file.


.. note::

   |TS| will start the |RPC| server without any need for configuration.


If a non-default configuration is needed, the following describes the structure.


File `jsonrpc.yaml` is a YAML format. The default configuration is::


   #YAML
   rpc:
      enabled: true
      unix:
         lock_path_name: /tmp/conf_jsonrpc.lock
         sock_path_name: /tmp/conf_jsonrpc.sock
         backlog: 5
         max_retry_on_transient_errors: 64
         restricted_api: true


===================== =========================================================================================
Field Name            Description
===================== =========================================================================================
``enabled``           Enable/disable toggle for the whole implementation, server will not start if this is false/no
``unix``              Specific definitions as per transport.
===================== =========================================================================================


IPC Socket (``unix``):

===================================== =========================================================================================
Field Name                            Description
===================================== =========================================================================================
``lock_path_name``                    Lock path, including the file name. (changing this may have impacts in :program:`traffic_ctl`)
``sock_path_name``                    Sock path, including the file name. This will be used as ``sockaddr_un.sun_path``. (changing this may have impacts in :program:`traffic_ctl`)
``backlog``                           Check https://man7.org/linux/man-pages/man2/listen.2.html
``max_retry_on_transient_errors``     Number of times the implementation is allowed to retry when a transient error is encountered.
``restricted_api``                    Used to set rpc unix socket permissions. If restricted `0700` will be set, otherwise `0777`. ``true`` by default.
===================================== =========================================================================================


.. note::

   Currently, there is only 1 communication mechanism supported. Unix Domain Sockets

