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
  * HostDB no longer supports persistent storage for DNS resolution
  * Removed support for the MMH crypto hash function

  * Traffic Manager is no longer part of |TS|. Administrative tools now interact with |TS| directly by using the :ref:`jsonrpc-node`.

    * traffic_ctl ``server``

      As a part of the above feature removal the ``backtrace``, ``restart``, ``start``, ``status`` options are no longer available in this |TS| version.

* Deprecated Features

  * Next Protocol Negotiation (NPN) support has been deprecated from ATS and will be removed in the next major release.

* Removed Libraries

  * mgmt_c - Client library for traffic_manager

Changes to Features
~~~~~~~~~~~~~~~~~~~
The following features have been changed in this version of ATS.

* Remap ACLs

   Changed in-line ACLs to match before activated ACL rules.
   For details refer to: https://github.com/apache/trafficserver/pull/11033 and :ref:`acl-filters`.

* Administrative API (RPC)

  Moved away from the binary serialization mechanism used to comunicate between |TS| and the tools to a JSON-RPC text based protocol. Underlying
  Unix Domain Socket protocol remains the same. Check :ref:`jsonrpc-protocol` for more details.

* Other changes

  * It is now a fatal error when ATS cannot bind or listen to a configured port
  * Propagate socket options specified in :ts:cv:`proxy.config.net.sock_option_flag_in` to newly accepted connections
  * HostDB internals were restructured, this should (externally) be backwards compatible.
    In any case you can check :ref:`developer-doc-hostdb` for more details.

API Changes
-----------
The following APIs have changed, either in semantics, interfaces, or both.

* Changed TS API

  * TSHttpTxnAborted
  * TSMimeHdrPrint
  * Enum values for hooks and events have been changed (ABI incompatible change)
  * TSSslSecretGet

* New TS API

  * TSContScheduleOnEntirePool
  * TSContScheduleEveryOnEntirePool

* Removed TS API

  * TSContSchedule
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


Cache
-----
The cache in this releases of ATS is compatible with previous versions of ATS. You would not expect
to lose your cache, or have to reinitialize the cache when upgrading.

Configuration Changes
---------------------
The following incompatible changes to the configurations have been made in this version of ATS.

The ``records.config`` file has been renamed to :file:`records.yaml` and now it is structured in YAML format.
Check :ref:`rec-config-to-yaml` and :file:`records.yaml`  for more details.

The following :file:`records.yaml` changes have been made:

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
- All ``proxy.config.ssl.TLSv*`` and ``proxy.config.ssl.client.TLSv*`` have been deprecated. Use
  ``proxy.config.ssl.server.version.min/max`` and ``proxy.config.ssl.client.version.min/max`` instead.
- The records.yaml entry ``proxy.config.http.keepalive_internal_vc`` has been removed.  This entry
  was previously undocumented.
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
- The records.yaml entry ``proxy.config.plugin.compiler_path`` has been added to specify an optional compiler tool path for compiling plugins.

The following changes have been made to the :file:`sni.yaml` file:

- ``disable_h2`` has been removed. Use ``http2`` with :code:`off` instead.
- The ``ip_allow`` key can now take a reference to a file containing the ip allow rules
- ``valid_tls_versions_in`` has been deprecated. Use ``valid_tls_version_min_in`` and ``valid_tls_version_max_in`` instead.
- Simplify wildcard support and matching order of the ``fqdn`` field

  * Allow single left-most ``*``
  * Do NOT support regex
  * Allow ``$1`` (capturing) support in the ``tunnel_route`` field
  * Matching depends on the order of entries (like :file:`remap.config`)

Plugins
-------

Removed Plugins
~~~~~~~~~~~~~~~
The following plugins have been removed from the ATS source code in this version of ATS:

  * mysql_remap - Dynamic remapping of URLs using data from a MySQL database.
  * acme
  * cache_key_genid
  * fast_cgi

Changes to Features
~~~~~~~~~~~~~~~~~~~
The following plugins have been changed in this version of ATS.

* regex_remap - matrix-parameters parameter has been removed. The string that follows a semicolon is now included in path.
* header_rewrite - MATRIX part specifier has been removed. The string that follows a semicolon is now included in PATH part.
* maxmind_acl - The regex part in its configuration takes the entire URL of a request, not just the path.
* rate_limit - Few changes were made on this plugin:

  * A ``YAML`` based configuration, reloadable even as global plugin.
  * SNI aliases
  * The IP reputation objects are now shareable for many SNIs.

  for more details, please check :ref:`admin-plugins-rate-limit`.

Lua Plugin
~~~~~~~~~~
* The following Http config constants have been renamed:

TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DEAD_SERVER has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_ATTEMPTS_MAX_RETRIES_DOWN_SERVER.

TS_LUA_CONFIG_HTTP_CONNECT_DEAD_POLICY has been renamed to TS_LUA_CONFIG_HTTP_CONNECT_DOWN_POLICY.

* The following functions have been removed:

  * ts.http.client_packet_tos_set
  * ts.http.server_packet_tos_set
  * ts.client_request.get_uri_params
  * ts.client_request.set_uri_params
  * ts.server_request.get_uri_params
  * ts.server_request.set_uri_params


Metrics
-------

- The HTTP connection metric proxy.process.http.dead_server.no_requests has been renamed to proxy.process.http.down_server.no_requests.
- The network metric ``proxy.process.net.calls_to_readfromnet_afterpoll`` has been removed
- The network metric ``proxy.process.net.calls_to_writetonet_afterpoll`` has been removed
- New cache metrics ``proxy.process.cache.stripes`` and
  ``proxy.process.cache.volume_X.stripes`` that counts cache stripes
- All metric names that ended in ``_stat`` have had that suffix dropped and no
  longer end with ``_stat``
- The metric ``proxy.node.cache.contents.num_doc`` was removed
- The metric ``proxy.node.config.reconfigure_required`` was renamed to
  ``proxy.process.proxy.reconfigure_required``
- The metric ``proxy.node.config.reconfigure_time`` was renamed to
  ``proxy.process.proxy.reconfigure_time``
- The metric ``proxy.node.config.restart_required.proxy`` was renamed to
  ``proxy.process.proxy.restart_required``
- The metric ``proxy.node.restarts.proxy.cache_ready_time`` was renamed to
  ``proxy.process.proxy.cache_ready_time``
- The metric ``proxy.node.restarts.proxy.stop_time`` was renamed to
  ``proxy.process.proxy.start_time``
- The following traffic_manager metrics have been removed:
  - proxy.node.hostname_FQ
  - proxy.node.hostname
  - proxy.node.proxy_running
  - proxy.node.restarts.proxy.restart_count
  - proxy.node.restarts.proxy.start_time
  - proxy.node.http.parent_proxy_total_response_bytes


Logging
-------

The ``cqtx`` log field has been removed, but can be replaced by ``cqhm pqu cqpv``.

The ``cqhv`` log field has been removed.

The ``cpu``, ``cquc``, ``cqup``, and ``cqus`` log fields have new names, ``pqu``, ``pquc``, ``pqup``, and ``pqus``. The old names have been deprecated.

The ``chi`` log field now represents the IP address of the previous hop if :ref:`Proxy Protocol <proxy-protocol>` is used.
