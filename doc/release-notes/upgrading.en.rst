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

The ``records.config`` file has been renamed to :file:`records.yaml` and now it is structured in YAML format.
Check :ref:`rec-config-to-yaml` and :file:`records.yaml`  for more details.

The records.yaml entry proxy.config.http.connect_attempts_max_retries_dead_server has been renamed to proxy.config.http.connect_attempts_max_retries_down_server.

- The records.yaml entry ``proxy.config.http.down_server.abort_threshold`` has been removed.
- The records.yaml entry ``proxy.config.http.connect_attempts_max_retries_dead_server`` has been renamed to :ts:cv:`proxy.config.http.connect_attempts_max_retries_down_server`.
- The entry ``proxy.config.http.connect.dead.policy`` has been renamed to :ts:cv:`proxy.config.http.connect.down.policy`.
- The records.yaml entry ``proxy.config.http.parent_proxy.connect_attempts_timeout`` and
  ``proxy.config.http.post_connect_attempts_timeout`` have been removed. Instead use
  :ts:cv:`proxy.config.http.connect_attempts_timeout` to control all connection to origin timeouts.
- The per server origin connection feature had a few configurations that were not used removed.
  ``proxy.config.http.per_server.connection.queue_size`` and ``proxy.config.http.per_server.connection.queue_delay``
  have been removed.
- The default value for records.yaml entry ``proxy.config.ssl.client.verify.server.policy`` has been changed
  from ``PERMISSIVE`` to ``STRICT``.
- The records.yaml entry ``proxy.config.http.keepalive_internal_vc`` has been removed.  This entry
  was previously undocumented.
- The records.yaml entries ``proxy.config.http.parent_proxy.connect_attempts_timeout`` and
  ``proxy.config.http.post_connect_attempts_timeout`` were previously referenced in default config
  files, but they did not have any effect.  These have been removed from default configs files.
- The default values for :ts:cv:`proxy.config.http.request_header_max_size`, :ts:cv:`proxy.config.http.response_header_max_size`, and
  :ts:cv:`proxy.config.http.header_field_max_size` have been changed to 32KB.
- The records.yaml entry :ts:cv:`proxy.config.http.server_ports` now also accepts the
  ``allow-plain`` option
- The records.yaml entry :ts:cv:`proxy.config.http.cache.max_open_write_retry_timeout` has been added to specify a timeout for starting a write to cache
- The records.yaml entry :ts:cv:`proxy.config.net.per_client.max_connections_in` has
  been added to limit the number of connections from a client IP. This works the
  same as :ts:cv:`proxy.config.http.per_server.connection.max`
- The records.yaml entry :ts:cv:`proxy.config.http.no_dns_just_forward_to_parent` is
  not overridable
- The records.yaml entry ``proxy.config.output.logfile`` has been renamed to :ts:cv:`proxy.config.output.logfile.name`.
- The records.yaml entry ``proxy.config.exec_thread.autoconfig`` has been renamed to :ts:cv:`proxy.config.exec_thread.autoconfig.enabled`.
- The records.yaml entry ``proxy.config.tunnel.prewarm`` has been renamed to :ts:cv:`proxy.config.tunnel.prewarm.enabled`.
- The records.yaml entry ``proxy.config.ssl.origin_session_cache`` has been renamed to :ts:cv:`proxy.config.ssl.origin_session_cache.enabled`.
- The records.yaml entry ``proxy.config.ssl.session_cache`` has been renamed to :ts:cv:`proxy.config.ssl.session_cache.enabled`.
- The records.yaml entry ``proxy.config.ssl.TLSv1_3`` has been renamed to :ts:cv:`proxy.config.ssl.TLSv1_3.enabled`.
- The records.yaml entry ``proxy.config.ssl.client.TLSv1_3`` has been renamed to :ts:cv:`proxy.config.ssl.client.TLSv1_3.enabled`.
- The records.yaml entry :ts:cv:`proxy.config.allocator.iobuf_chunk_sizes` has been added
  to enable more control of iobuffer allocation.
- The records.yaml entry :ts:cv:`proxy.config.allocator.hugepages` will enable
  allocating iobuffers and cache volumes from hugepages if configured in the
  system.

The following changes have been made to the :file:`sni.yaml` file:

- ``disable_h2`` has been removed. Use ``http2`` with :code:`off` instead.
- The ``ip_allow`` key can now take a reference to a file containing the ip
  allow rules

The records.yaml entry proxy.config.http.parent_proxy.connect_attempts_timeout and proxy.config.http.post_connect_attempts_timeout
have been removed. Instead use proxy.config.http.connect_attempts_timeout to control all connection to origin timeouts.

Plugins
-------

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
