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

.. include:: ../common.defs

.. _upgrading:

Upgrading to ATS v10.x
======================

.. toctree::

Feature Changes
---------------

Removed and Deprecated Features
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The following features, configurations and plugins are either removed or deprecated in this
version of ATS. Deprecated features should be avoided, with the expectation that they will be
removed in the next major release of ATS.

* Removed Features

* Deprecated Features

  * Next Protocol Negotiation (NPN) support has been deprecated from ATS and will be removed in the next major release.

Changes to Features
~~~~~~~~~~~~~~~~~~~
The following features have been changed in this version of ATS.

* Remap ACLs

   Fixes with @action=allow to deny transactions that are not explicitly allowed.  Changed in-line ACLs to match before activated ACL rules.
   For detail refer to: https://github.com/apache/trafficserver/pull/11033


API Changes
-----------
The following APIs have changed, either in semantics, interfaces, or both.

* Changed TS API

  * TSHttpTxnAborted

* Removed TS API

  * TSHttpSsnArgSet
  * TSHttpSsnArgGet
  * TSHttpSsnArgIndexReserve
  * TSHttpSsnArgIndexNameLookup
  * TSHttpSsnArgIndexLookup
  * TSHttpTxnArgSet
  * TSHttpTxnArgGet
  * TSHttpTxnArgIndexReserve
  * TSHttpTxnArgIndexNameLookup
  * TSHttpTxnArgIndexLookup
  * TSHttpTxnClientPacketTosSet
  * TSHttpTxnServerPacketTosSet
  * TSMgmtConfigIntSet
  * TSUrlHttpParamsGet
  * TSUrlHttpParamsSet
  * TSVConnArgSet
  * TSVConnArgGet
  * TSVConnArgIndexReserve
  * TSVConnArgIndexNameLookup
  * TSVConnArgIndexLookup
  * TSRecordType::TS_RECORDTYPE_CLUSTER
  * TSRecordType::TS_RECORDTYPE_LOCAL

* Removed INK UDP API

  * INKUDPBind
  * INKUDPSendTo
  * INKUDPRecvFrom
  * INKUDPConnFdGet
  * INKUDPPacketCreate
  * INKUDPPacketBufferBlockGet
  * INKUDPPacketFromAddressGet
  * INKUDPPacketFromPortGet
  * INKUDPPacketConnGet
  * INKUDPPacketDestroy
  * INKUDPPacketGet

* Removed Lua API

  * ts.http.client_packet_tos_set
  * ts.http.server_packet_tos_set
  * ts.client_request.get_uri_params
  * ts.client_request.set_uri_params
  * ts.server_request.get_uri_params
  * ts.server_request.set_uri_params


Cache
-----
The cache in this releases of ATS is compatible with previous versions of ATS. You would not expect
to lose your cache, or have to reinitialize the cache when upgrading.

Configuration Changes
---------------------
The following incompatible changes to the configurations have been made in this version of ATS.

The ``records.config`` file has been renamed to :file:`records.yaml` and now it is structured in YAML format.

The following :file:`records.yaml` chnages have been made:

- The records.yaml entry ``proxy.config.http.down_server.abort_threshold`` has been removed.
- The records.yaml entry ``proxy.config.http.connect_attempts_max_retries_dead_server`` has been renamed to ``proxy.config.http.connect_attempts_max_retries_down_server``.
- The entry ``proxy.config.http.connect.dead.policy`` has been renamed to ``proxy.config.http.connect.down.policy``.
- The records.yaml entry ``proxy.config.http.parent_proxy.connect_attempts_timeout`` and
  ``proxy.config.http.post_connect_attempts_timeout`` have been removed. Instead use
  ``proxy.config.http.connect_attempts_timeout`` to control all connection to origin timeouts.
- The per server origin connection feature had a few configurations that were not used removed.
  ``proxy.config.http.per_server.connection.queue_size`` and ``proxy.config.http.per_server.connection.queue_delay``
  have been removed.

Plugins
-------

Removed Plugins
~~~~~~~~~~~~~~~
The following plugins have been removed from the ATS source code in this version of ATS:

  * mysql_remap - Dynamic remapping of URLs using data from a MySQL database.

Changes to Features
~~~~~~~~~~~~~~~~~~~
The following plugins have been changed in this version of ATS.

* regex_remap - matrix-parameters parameter has been removed. The string that follows a semicolon is now included in path.
* header_rewrite - MATRIX part specifier has been removed. The string that follows a semicolon is now included in PATH part.

Lua Plugin
~~~~~~~~~~
The following Http config constants have been renamed:

TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER.

TS_LUA_CONFIG_HTTP_CONNECT_DEAD_POLICY has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_DOWN_POLICY.

Metrics
-------

The HTTP connection metric proxy.process.http.dead_server.no_requests has been renamed to proxy.process.http.down_server.no_requests.

Logging
-------

The ``cqtx`` log field has been removed, but can be replaced by ``cqhm pqu cqpv``.

The ``cqhv`` log field has been removed.

The ``cpu``, ``cquc``, ``cqup``, and ``cqus`` log fields have new names, ``pqu``, ``pquc``, ``pqup``, and ``pqus``. The old names have been deprecated.
