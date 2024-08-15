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
   :maxdepth: 1

Deprecated or Removed Features
------------------------------
The following features, configurations and plugins are either removed or deprecated in this
version of ATS. Deprecated features should be avoided, with the expectation that they will be
removed in the next major release of ATS.

* Removed TS API

  * TSHttpTxnArgSet
  * TSHttpTxnArgGet
  * TSHttpSsnArgSet
  * TSHttpSsnArgGet
  * TSVConnArgSet
  * TSVConnArgGet
  * TSHttpTxnArgIndexReserve
  * TSHttpTxnArgIndexNameLookup
  * TSHttpTxnArgIndexLookup
  * TSHttpSsnArgIndexReserve
  * TSHttpSsnArgIndexNameLookup
  * TSHttpSsnArgIndexLookup
  * TSVConnArgIndexReserve
  * TSVConnArgIndexNameLookup
  * TSVConnArgIndexLookup


API Changes
-----------
The following APIs have changed, either in semantics, interfaces, or both.


Cache
-----
The cache in this releases of ATS is compatible with previous versions of ATS. You would not expect
to lose your cache, or have to reinitialize the cache when upgrading.

Configuration changes
---------------------
The following incompatible changes to the configurations have been made in this version of ATS.

The records.yaml entry proxy.config.http.down_server.abort_threshold has been removed.

The records.yaml entry proxy.config.http.connect_attempts_max_retries_dead_server has been renamed to proxy.config.http.connect_attempts_max_retries_down_server.

The records.yaml entry proxy.config.http.connect.dead.policy has been renamed to proxy.config.http.connect.down.policy.

The records.yaml entry proxy.config.http.parent_proxy.connect_attempts_timeout and proxy.config.http.post_connect_attempts_timeout
have been removed. Instead use proxy.config.http.connect_attempts_timeout to control all connection to origin timeouts.

Plugins
-------

Removed Plugins
~~~~~~~~~~~~~~~
The following plugins have been removed from the ATS source code in this version of ATS:

  * mysql_remap - Dynamic remapping of URLs using data from a MySQL database.

Lua Plugin
~~~~~~~~~~
The following Http config constants have been renamed:

TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER.

TS_LUA_CONFIG_HTTP_CONNECT_DEAD_POLICY has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_DOWN_POLICY.

Metrics
------------------

The HTTP connection metric proxy.process.http.dead_server.no_requests has been renamed to proxy.process.http.down_server.no_requests.

Logging
------------------

The ``cqtx`` log field has been removed, but can be replaced by ``cqhm pqu cqpv``.
