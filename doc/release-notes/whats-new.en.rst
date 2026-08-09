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

.. _whats_new:


What's New in ATS v10.2
=======================

This version of |ATS| includes 1034 commits from 655 pull requests, with 44
contributors participating in this development cycle.

Configuration Reload
--------------------

Configuration reload has been rebuilt around a *token* model that makes a
reload an observable, trackable operation rather than a fire-and-forget
signal. Each reload gets a token; handlers report progress and terminal state
against that token, so a reload can be monitored to completion and its
per-handler logs inspected. See :doc:`../developer-guide/config-reload-framework.en`.

* ``traffic_ctl config reload`` gained ``--monitor``/``-m`` to follow a reload
  to completion, ``--show-details``/``-s`` and ``--include-logs``/``-l`` for
  per-handler detail, ``--token``/``-t`` to target a specific token,
  ``--refresh-int``/``-r`` and ``--timeout``/``-T`` to control polling, and
  ``--force``/``-F``.
* ``traffic_ctl config status`` reports reload records for a token, with
  ``--count``/``-c`` (numeric or ``all``) and ``--min-level`` to filter log
  severity.
* Reload behavior is tunable with :ts:cv:`proxy.config.admin.reload.timeout`
  and :ts:cv:`proxy.config.admin.reload.check_interval`.
* Replaced configurations are now destroyed on an ``ET_TASK`` thread rather
  than on a network thread, so a large config teardown no longer blocks an
  event loop.

Features
--------

* Implement RFC 9213 Targeted HTTP Cache Control. Cache directives can be
  targeted at specific caches via headers such as ``CDN-Cache-Control``,
  configured through
  :ts:cv:`proxy.config.http.cache.targeted_cache_control_headers`, which is
  overridable per remap rule. When a targeted header is present it takes
  precedence over the standard ``Cache-Control`` header, and the targeted
  header is passed downstream so cache hierarchies behave correctly.
* Connect retries to the origin can now back off exponentially, controlled by
  :ts:cv:`proxy.config.http.connect_attempts_retry_backoff_base`, instead of
  retrying immediately and piling connections onto a struggling origin.
* Origin connect retry limits are now selected by HostDB state:
  :ts:cv:`proxy.config.http.connect_attempts_max_retries` for ``UP`` servers,
  the new
  :ts:cv:`proxy.config.http.connect_attempts_max_retries_suspect_server` for
  ``SUSPECT``, and no retries for ``DOWN``. The retry limits were not
  previously applied according to origin state, so this is a necessary
  incompatible change. See :ref:`upgrading` before deploying.
* :ts:cv:`proxy.config.http.connect.down.policy` gained option ``3``, which
  counts inactive connections as failures.
* Snowflake IDs, 64-bit organizationally unique identifiers, are now used for
  connection IDs so they are unique across restarts and typically across
  instances in a CDN.
* TLS certificate compression is supported in both directions, configured with
  :ts:cv:`proxy.config.ssl.server.cert_compression.algorithms` and
  :ts:cv:`proxy.config.ssl.client.cert_compression.algorithms`.
* Certificate loading can now be parallelized with
  :ts:cv:`proxy.config.ssl.server.multicert.concurrency`.
* :file:`sni.yaml` supports session ticket overrides.
* QUIC token secrets are configurable via
  ``proxy.config.quic.server.token_key.filename``.
* The PROXY protocol header size limit is configurable with
  :ts:cv:`proxy.config.proxy_protocol.max_header_size`.
* A per-client connection limit exempt list is available through
  :ts:cv:`proxy.config.http.per_client.connection.exempt_list`, the new
  ``connection_exempt_list`` plugin, and the TS API (see below).
* Parsed values for expensive STRING configurations (for example
  ``negative_caching_list``, ``insert_forwarded``,
  ``server_session_sharing.match``) are now cached automatically, so repeated
  ``TSHttpTxnConfigStringSet()`` calls with the same value parse only once.
* The migration from PCRE to PCRE2 is complete; all remaining PCRE references
  have been removed from the core and plugins.

Configuration
-------------

New :file:`records.yaml` settings in this release:

* :ts:cv:`proxy.config.admin.reload.timeout`
* :ts:cv:`proxy.config.admin.reload.check_interval`
* :ts:cv:`proxy.config.cache.default_volumes`
* :ts:cv:`proxy.config.cache.dir.sync_parallel_tasks`
* :ts:cv:`proxy.config.cache.ram_cache.s3fifo.ghost_mem_percent`
* :ts:cv:`proxy.config.cache.ram_cache.s3fifo.ghost_size_percent`
* :ts:cv:`proxy.config.cache.ram_cache.s3fifo.main_percent`
* :ts:cv:`proxy.config.cache.ram_cache.s3fifo.promote_threshold`
* :ts:cv:`proxy.config.exec_thread.loop_time_update_probability`
* :ts:cv:`proxy.config.exec_thread.watchdog.timeout_ms`
* :ts:cv:`proxy.config.http.cache.targeted_cache_control_headers`
* :ts:cv:`proxy.config.http.connect_attempts_max_retries_suspect_server`
* :ts:cv:`proxy.config.http.connect_attempts_retry_backoff_base`
* :ts:cv:`proxy.config.http.parent_proxy.consistent_hash_algorithm`
* :ts:cv:`proxy.config.http.per_client.connection.exempt_list`
* :ts:cv:`proxy.config.proxy_protocol.max_header_size`
* ``proxy.config.quic.server.token_key.filename``
* :ts:cv:`proxy.config.ssl.client.cert_compression.algorithms`
* :ts:cv:`proxy.config.ssl.server.cert_compression.algorithms`
* :ts:cv:`proxy.config.ssl.server.multicert.concurrency`

Other configuration changes:

* :ts:cv:`proxy.config.ssl.max_record_size` now accepts the documented ``-1``
  value, making dynamic TLS record sizing reachable from :file:`records.yaml`.
* :ts:cv:`proxy.config.ssl.client.CA.cert.filename` is now overridable.
* ``negative_caching_list`` and ``negative_revalidating_list`` are overridable.
* ``traffic_ctl config reset`` resets configuration values matching a path
  pattern back to their defaults.
* ``traffic_ctl hostdb status`` is a new command for inspecting HostDB state.
* ``traffic_ctl`` gained a global ``--watch``/``-w`` option to re-run a command
  periodically.
* JSONRPC now refuses writes to records marked ``RECA_READ_ONLY`` or
  ``RECA_NO_ACCESS``.

Metrics
-------

* Added ``proxy.process.http.000_responses``
* Added ``proxy.process.http.429_responses``
* Added ``proxy.process.log.marshalled_bytes``
* Added ``proxy.process.net.per_client.connections_exempt_in``
* Added ``proxy.process.ssl.connections_closed``
* Added ``proxy.process.ssl.total_handshake_bytes_read_in``
* Added ``proxy.process.ssl.total_handshake_bytes_write_out``
* Added ``proxy.process.ssl.ssl_session_cache_timeout``
* Added ``proxy.process.ssl.ssl_origin_session_cache_timeout``
* Added ``proxy.process.ssl.handshake_sign_rsa``,
  ``proxy.process.ssl.handshake_sign_ecdsa`` and
  ``proxy.process.ssl.handshake_sign_other`` to count handshake signatures by
  key type
* Added TLS certificate compression counters
  ``proxy.process.ssl.cert_compress.{zlib,brotli,zstd}`` and
  ``proxy.process.ssl.cert_decompress.{zlib,brotli,zstd}``, each with a
  matching ``_failure`` counter
* Added ``proxy.process.plugin.header_rewrite.conditions`` and
  ``proxy.process.plugin.header_rewrite.operators``
* Added ``proxy.process.plugin.compress.bytes_in``
* Added per-plugin workload counters under the ``proxy.process.plugin.``
  prefix
* Added per-curve TLS handshake time metrics and a cache stripe lock
  contention metric
* ``proxy.process.http.incoming_requests`` is now counted at transaction
  start

Logging
-------

New log fields in this release:

* ``chiv`` - verified client host IP
* ``ckh`` - cache key hash
* ``cqqtl`` - client request squid length including TLS overhead
* ``cqssrt`` - TLS session resumption type
* ``cthb``, ``cthbr``, ``cthbt`` - client TLS handshake bytes (total, received,
  transmitted)
* ``mstsms`` - server-side TLS handshake milestone
* ``pptc``, ``pptg``, ``pptv`` - PROXY protocol TLS cipher, group and version
* ``prscs`` - the component that set the proxy response status code
* ``psfid`` - process Snowflake ID
* ``psqtl`` - proxy response squid length including TLS overhead

Other logging changes:

* Plugins can register custom log fields at runtime with ``TSLogFieldRegister``
  and the ``TSLog*Marshal`` functions.
* Added the ``ERR_TUN_ACTIVE_TIMEOUT`` squid code for tunnel timeouts.

Plugins
-------

New plugins:

* ``connection_exempt_list`` - manage the per-client connection limit exempt
  list
* ``filter_body`` - filter request and response body content
* ``jax_fingerprint`` - consolidated JA3/JA4 fingerprinting
* ``realip`` - set the verified client address from a trusted source

``redo_cache_lookup`` has been moved out of the experimental plugins and into
the examples.

header_rewrite and hrw4u:

* :program:`hrw4u` is a new DSL and compiler for ``header_rewrite``
  configurations, with a companion ``u4wrh`` tool that converts existing
  ``header_rewrite`` configuration back into the DSL. See
  :doc:`../admin-guide/configuration/hrw4u.en`.
* ``header_rewrite`` can invoke the hrw4u compiler directly at config load.
* Added ``elif`` support in ``if``/``elif``/``else`` chains, nested ``if``,
  ``SETS`` with partial string matching, session-scope state variables,
  ``SERVER-HEADER`` and ``SERVER-URL``, indexed query parameters,
  ``set-effective-address``, ``set-cc-alg``, and ``POST_REMAP_HOOK`` support.
* Per-remap MaxMind geo database handles are supported.
* Bad ``run-plugin`` directives are now rejected at config load rather than at
  runtime.

Other plugin changes:

* compress: Zstandard support, content-type parameter handling, and an option
  to skip compressing partial objects.
* stats_over_http: a Prometheus v2 output format that groups samples by metric
  family and derives labels for methods, directions, status codes, cache
  results, time buckets and cache volumes; plus ``HINT`` and ``TYPE``
  annotations.
* xdebug: a ``probe-full-json`` feature that emits the full probe output as
  JSON, including the encoded origin body.
* escalate: added the ``x-escalate-redirect`` header and
  ``--escalate-non-get-methods``.
* esi: added ``--allowed-response-codes``.
* maxmind_acl: added a bypass header configuration option.
* slice: prefetch deduplication and a freelist, and purge now covers every
  block of an object rather than stopping at the first gap.
* lua: support for Unix domain socket inbound connections, the verified
  address API, PROXY protocol info, certificate introspection, the connection
  exempt list, and a shutdown hook.
* Cripts: cache group concepts, geo APIs on ``cripts::IP``, a refactored cache
  key / URL API, and a substantially smaller per-transaction ``Context``.

TS API
------

* ``TSVConnClientHelloGet`` and ``TSClientHelloExtensionGet`` provide access to
  the TLS ClientHello and its extensions.
* ``TSHttpTxnVerifiedAddrSet`` / ``TSHttpTxnVerifiedAddrGet`` set and read a
  verified client address.
* ``TSHttpTxnCacheKeyDigestGet`` returns the cache key hash.
* ``TSLogFieldRegister``, ``TSLogIntMarshal``, ``TSLogStringMarshal`` and
  ``TSLogAddrMarshal`` allow plugins to define custom log fields.
* ``TSConnectionLimitExemptListAdd``, ``TSConnectionLimitExemptListRemove`` and
  ``TSConnectionLimitExemptListClear`` manage the per-client connection limit
  exempt list.
* ``TSHttpTxnNextHopStrategySet``, ``TSHttpTxnNextHopStrategyGet`` and
  ``TSHttpTxnParentStrategyGet`` expose next hop strategy selection.
* ``TSMutexLockGuard`` is an RAII guard for ``TSMutex``.


What's New in ATS v10.1
=======================

Metrics
-------

* Added ``proxy.process.http.total_parent_marked_down_timeout``
* Added ``proxy.process.http.total_client_connections_uds``
* Added ``proxy.process.ssl.group.user_agent.P-256``
* Added ``proxy.process.ssl.group.user_agent.P-384``
* Added ``proxy.process.ssl.group.user_agent.P-521``
* Added ``proxy.process.ssl.group.user_agent.X25519``
* Added ``proxy.process.ssl.group.user_agent.P-224``
* Added ``proxy.process.ssl.group.user_agent.X448``
* Added ``proxy.process.ssl.group.user_agent.X25519MLKEM768``

Plugins
-------

* stats_over_http: Add prometheus metrics format option
* header_rewrite: Add ``set-plugin-cntl`` operator
* header_rewrite: Add ``LAST-CAPTURE`` condition to access the last capture
  group of a regex
* header_rewrite: Add support for state variables that can be used in conditions
  and operators.
* header_rewrite: Add support for an else clause in conditions
* header_rewrite: Add a ``GROUP`` condition
* header_rewrite: Add a ``HTTP-CNTL`` condition to control if expensive rules
  are run.
* header_rewrite: Add the ``set-body-from`` operator to set the response body
  from a URL
* header_rewrite: The ``set-body-from`` operator now defers renabling the
  transaction until after the fetch of the URL providing the response body
* slice: Support unix domain socket paths
* slice: Add configuration to limit slicing of some objects.
* access_control: Generate a session cookie when ``exp=0`` appears in a
  ``TokenRespHdr`` origin response header.
* compress:  Add range request control options to adjust behavior based on the
  ``Accept-Encoding`` or ``Range`` headers
* lua: Add support for millisecond sleep
* escalate: Now handles dispatching to the failover server if the original server is down
* ja3_fingerprint: Add the ``--preserve`` option to avoid modifing some existing
  ja* fields.
* ja4_fingerprint: Added this new plugin
* rate_limit: Add a ``--rate`` option to limit by RPS

TS API
------

* Add ``TSVConnPPInfoGet`` to get Proxy Protocol information.
* Add ``TSContScheduleOnEntirePool`` and ``TSContScheduleEveryOnEntirePool`` to
  schedule continuations on every thread in a pool.

Features
--------

* Add the ``cqssg`` log field for TLS group name logging
* traffic_ctl: Add a new :ref:`server <traffic-control-command-server-status>` command to show some basic internal
  information
* traffic_ctl: Now displays YAML format output when the ``--records`` option is
  set.
* traffic_ctl: Added the ``server debug`` command to enable/disable diagnostics
  and debug tags at runtime with a single command.
* cripts: Add some new high level  :ref:`convenience <cripts-convenience>` APIs
* cripts: Add optional reason parameter to ``Error::Status``
* sni.yaml: Add ``server_cipher_suite`` and ``server_TLSv1_3_cipher_suites`` to
  allow overriding the setting from ``records.yaml``
* Add support for getting authority information from Proxy Protocol V2. with new
  ``ppa`` log formatter.
* Add support for getting UDP address info from Proxy Protocol.
* Added support for listening on a Unix Domain Socket. See :ts:cv:`proxy.config.http.server_ports`
* Added option for :ts:cv:`proxy.config.http.auth_server_session_private` to only mark the connection private if ``Proxy-Authorization`` or ``Www-Authenticate`` headers are present
* It is now an ``ERROR`` if a remap ACL has more than one ``@action`` parameter.
  This was an error in ATS 10.0.x
* Add a ``fragment-size`` option in volume.config to control the fragment size
  of the volume.
* Add an optional ``avg_obj_size`` to ``volume.config`` to control the directory
  entry sizing.
* The ``proxy.config.http.cache.post_method`` is now an overridable config.
* Defer deleting the copied plugin shared object file to startup to make it
  easier to debug crashes in plugins.


Configuration
-------------

* Added :ts:cv:`proxy.config.http.negative_revalidating_list` to configure the
  list of status codes that apply to the negative revalidating feature
* Added :ts:cv:`proxy.config.ssl.session_cache.mode` to control TLS session caching.
  This is intended to replace the legacy :ts:cv:`proxy.config.ssl.session_cache.enabled` and
  ``proxy.config.ssl.session_cache.value`` configurations. The
  :ts:cv:`proxy.config.ssl.session_cache.enabled` setting was documented but
  never implemented, while ``proxy.config.ssl.session_cache.value`` was
  implemented but not documented. The new :ts:cv:`proxy.config.ssl.session_cache.mode`
  functions just like the legacy ``proxy.config.ssl.session_cache.value`` did
  in the ealier 10.0 release. The :ts:cv:`proxy.config.ssl.session_cache.mode`
  setting provides a clear and consistent interface going forward.  For backward
  compatibility, ``.enabled`` is now implemented, but both ``.enabled`` and
  ``.value`` will be removed in ATS 11.x.



What's New in ATS v10.0
=======================


This version of |ATS| includes over <x> commits, from <y> pull requests. A
total of <z> contributors have participated in this development cycle.

.. toctree::
   :maxdepth: 1

New Features
------------

* JSON-RPC based interface for administrative API

   |TS| now exposes a JSON-RPC node to interact with external tools. Check :ref:`developer-guide-jsonrpc` for more details.

* traffic_ctl has a new command ``monitor`` to show a continuously updating list of metrics

* :file:`ip_allow.yaml` and :file:`remap.config` now support named IP ranges via IP
  Categories. See the ``ip_categories`` key definition in :file:`ip_allow.yaml`
  for information about their use and definitions.

* :file:`sni.yaml` ``fqdn:tunnel_route``, beside the already supported match group
  number, configuration now also supports the destination port using a variable specification
  either for the incoming connection port or the port that was specified by the
  incoming Proxy Protocol payload. Check :file:`sni.yaml` for more information.

* The records.yaml entry :ts:cv:`proxy.config.system_clock` was added to control the underlying
  system clock that ATS uses for internal timing

* OCSP requests is now be able to use GET method. See :ts:cv:`proxy.config.ssl.ocsp.request_mode` for more information.

* TSHttpSsnInfoIntGet has been added.

New or modified Configurations
------------------------------

ip_allow.yaml and remap.config ACL actions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There are two new sets of actions for HTTP request method filtering introduced in |TS| 10.x:

- Both :file:`ip_allow.yaml` and :file:`remap.config` now support the ``set_allow`` and ``set_deny`` actions. These
  actions both behave like ``allow`` and ``deny`` did for :file:`ip_allow.yaml` pre |TS| 10.x.
- In addition, :file:`remap.config` now supports ``add_allow`` and ``add_deny`` actions. These behave like ``allow``
  and ``deny`` actions did for :file:`remap.config` ACLs pre |TS| 10.x.

The details about the motivation and behavior of these actions are documented in :ref:`acl-filters`.

Logging and Metrics
-------------------

The numbers of HTTP/2 frames received have been added as metrics.

Plugins
-------

* authproxy - ``--forward-header-prefix`` parameter has been added
* prefetch - Cmcd-Request header support has been added
* xdebug - ``--enable`` option to selectively enable features has been added
* system_stats - Stats about memory have been added
* slice plugin - This plugin was promoted to stable.
* compress plugin - Added support for Zstandard (zstd) compression algorithm.

JSON-RPC
^^^^^^^^

   Remote clients, like :ref:`traffic_ctl_jsonrpc` have now bi-directional access to the plugin space. For more details check :ref:`jsonrpc_development`.

Replaced autotools build system with cmake
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

See :ref:`admin-installing` for more information

Switch to C++20
^^^^^^^^^^^^^^^

Plugins are now required to be compiled as C++ code, rather than straight C.
The API is tested with C++20, so code compatible with this version is preferred.
``TSDebug`` and related functions are removed.  Debug tracing should now be done
using cpp:func:`Dbg` and related functions, as in |TS| core code.

C++ Plugin API Deprecated
^^^^^^^^^^^^^^^^^^^^^^^^^

It is deprecated in this release.  It will be deleted in ATS 11.

Symbols With INKUDP Prefix
^^^^^^^^^^^^^^^^^^^^^^^^^^

In the plugin API, all types and functions starting with the prefix INKUDP are removed.

New plugin hook for request sink transformation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A new hook, ``TS_HTTP_REQUEST_CLIENT_HOOK``, has been added. This provides the analoguas functionality of
``TS_HTTP_RESPONSE_CLIENT_HOOK``, for request bodies.

HTTP/2
^^^^^^

* Support for HTTP/2 on origin server connections has been added. This is disabled by default. For more details check :ts:cv:`proxy.config.ssl.client.alpn_protocols`
* Support for CONNECT method has been added.
* Window size control has been improved. For more details check :ts:cv:`proxy.config.http2.flow_control.policy_in`

HTTP UI Removed
^^^^^^^^^^^^^^^

The stats and cache inspector pages were unmaintained and removed in this
release.


