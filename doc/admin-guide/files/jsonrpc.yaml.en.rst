
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
.. configfile:: jsonrpc.yaml

.. _admin-jsonrpc:


jsonrpc.yaml
************

.. _admin-jsonrpc-description:

The :file:`jsonrpc.yaml` file defines some of the configurable arguments of the
jsonrpc endpoint.

Description
===========

|TS| Implements and exposes management calls using a JSONRPC API.  This API is
base on the following two things:

* `JSON  <https://www.json.org/json-en.html>`_  format. Lightweight
  data-interchange format. It is easy for humans to read and write.
  It is easy for machines to parse and generate. It's basically a  collection of
  name/value pairs.

* `JSONRPC 2.0 <https://www.jsonrpc.org/specification>`_ protocol. Stateless,
  light-weight remote procedure call (RPC) protocol.
  Primarily this specification defines several data structures and the rules
  around their processing.


In order for programs to communicate with |TS|, the server exposes a
``JSONRRPC 2.0`` API where programs can communicate with it. In this document you
will find some of the configurable arguments that can be changed.


.. _admin-jsonrpc-configuration:

Configuration
=============

.. note::

   |TS| will start the |RPC| server without any need for configuration.

If a non-default configuration is needed, the following describes the configuration
structure.

File `jsonrpc.yaml` is a YAML format. The default configuration looks like:

.. code-block:: yaml

   rpc:
     enabled: true
     unix:
       restricted_api: false


===================== ==========================================================
Field Name            Description
===================== ==========================================================
``enabled``           Enable/disable toggle for the whole implementation, server
                      will not start if this is false/no
``unix``              Specific definitions as per transport.
===================== ==========================================================


IPC Socket (``unix``)
---------------------

Unix Domain Socket related configuration.

===================================== =========================================================================================
Field Name                            Description
===================================== =========================================================================================
``lock_path_name``                    Lock path, including the file name. (changing this may have impacts in :program:`traffic_ctl`)
``sock_path_name``                    Sock path, including the file name. This will be used as ``sockaddr_un.sun_path``. (changing
                                      this may have impacts in :program:`traffic_ctl`)
``backlog``                           Check https://man7.org/linux/man-pages/man2/listen.2.html
``max_retry_on_transient_errors``     Number of times the implementation is allowed to retry when a transient error is encountered.
``restricted_api``                    This setting specifies whether the jsonrpc node access should be restricted to root processes.
                                      If this is set to ``false``, then on platforms that support passing process credentials, non-root
                                      processes will be allowed to make read-only JSONRPC calls. Any calls that modify server state
                                      (eg. setting a configuration variable) will still be restricted to root processes. If set to ``true``
                                      then only root processes will be allowed to perform any api call.
                                      If restricted, the Unix Domain Socket will be created with `0700` permissions, otherwise `0777`.
                                      ``true`` by default.
                                      In case of an unauthorized call is made, a corresponding rpc error will be returned, you can
                                      check :ref:`jsonrpc-node-errors-unauthorized-action` for details about the errors.
``incoming_request_max_size``         Maximum allowed size for the incoming jsonrpc request. Default ``96000`` bytes. Size must be
                                      specified in bytes. Note that memory will not be allocated all at once, if incoming message
                                      does not fit in the first chunk of memory(32K) an extra amount will be allocated, till the
                                      requests size hits the max size.
===================================== =========================================================================================


.. note::

   Currently, there is only 1 communication mechanism supported. Unix Domain Sockets


See also
========

:ref:`traffic_ctl_jsonrpc`
