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

Header Rewrite & HRW4U
----------------------

* HRW4U: A new DSL for ``header_rewrite`` configuration that provides a more
  conventional and readable syntax. Includes a compiler that translates HRW4U
  into native header_rewrite rules, and ``u4wrh``, an inverse tool that
  converts existing header_rewrite rules back to HRW4U syntax.
* header_rewrite: Add partial string matching modifiers: ``[PRE]``, ``[SUF]``,
  ``[MID]``, ``[EXT]``
* header_rewrite: Add ``SETS`` for matching against a set of values, with
  support for quoted strings containing commas
* header_rewrite: Add ``elif`` support in ``if-elif-else`` conditionals
* header_rewrite: Add support for nested ``if`` conditionals
* header_rewrite: Add ``set-effective-address`` operator to set the client's
  effective (verified) address
* header_rewrite: Add ``set-cc-alg`` operator to set the congestion control
  algorithm per remap
* header_rewrite: Add ``SERVER-HEADER`` and ``SERVER-URL`` conditions
* header_rewrite: Add indexed query parameter conditions
* header_rewrite: Add optional ``--timezone`` and ``--inbound-ip-source``
  plugin load switches

Plugins
-------

* New plugin: ``filter_body`` for request/response body content inspection
  with configurable pattern matching and actions (log, block, add_header)
* New plugin: ``real-ip`` with ``TSHttpTxnVerifiedAddrSet/Get`` API for
  verified client IP address management
* compress: Full Zstandard (zstd) compression support with new
  ``proxy.config.http.normalize_ae`` modes 4 and 5
* compress: Add ``content_type_ignore_parameters`` option to match
  Content-Type patterns ignoring charset parameters
* compress: Add option to not compress partial objects
* escalate: Add ``x-escalate-redirect`` header indicator when escalation
  occurs (disable via ``--no-redirect-header``)
* escalate: Add ``--escalate-non-get-methods`` to enable escalation of
  non-GET requests
* xdebug: Add ``probe-full-json`` feature for complete JSON diagnostic
  output
* ESI: Add ``--allowed-response-codes`` for response code filtering
* stats_over_http: Add ``HINT`` and ``TYPE`` Prometheus annotations with
  metric type information
* lua: Add support for Unix socket incoming connections
* lua: Add proxy protocol information access API
* lua: Add verified address get/set API
* lua: Add certificate information retrieval (subject, issuer, serial,
  SANs, etc.)
* lua: Add connection exempt list API support
* cookie_remap: Add ``disable_pristine_host_hdr`` configuration parameter
* ja3_fingerprint/ja4_fingerprint: Add ``x-ja3-via`` and ``x-ja4-via``
  headers for multi-proxy fingerprint attribution
* slice/cache_range_requests: Avoid subsequent IMS requests by using
  identifier-based freshness checking
* origin_server_auth: Exclude hop-by-hop headers from AWS v4 signature
  calculation
* ``prscs``: New log field for proxy response status code setter, identifying
  which component (plugin, ip_allow, etc.) set the response status

Cripts
------

* Add Cache Groups concepts for cache routing
* Add Geo APIs to the ``cripts::IP`` object for geographic lookups
* Refactor cache key / URL APIs with cleaner abstractions
* Add ``connection_exempt_list.cript`` for per-client connection max
  exempt list management
* Build system support for pre-compiled cripts via ``add_cript`` in
  CMakeLists.txt

Cache
-----

* Implement RFC 9213 Targeted HTTP Cache Control (e.g.,
  ``CDN-Cache-Control``) via configurable
  :ts:cv:`proxy.config.http.cache.targeted_cache_control_headers`
* Cache volumes: Add RAM cache settings and ``@volume=`` remap option in
  ``volume.config``
* Add parallel directory entry sync options for faster cache sync with
  configurable parallelism
* Add fail action 6: fallback to serving stale content when retry attempts
  are exhausted
* 9.2/10.x cache key compatibility mode for seamless upgrades without
  cache invalidation

TLS/SSL
-------

* Add per-curve/group TLS handshake time metrics
* Add server-side TLS handshake milestones
  (``TS_MILESTONE_SERVER_TLS_HANDSHAKE_START/END``)
* Add ``cqssrt`` log field for TLS resumption type (none, session cache,
  or ticket)
* Dynamic TLS group discovery via ``SSL_CTX_get0_implemented_groups``
  including KEM groups (X25519MLKEM768, SecP256r1MLKEM768)
* Parallel SSL certificate loading support
* sni.yaml: Add session ticket override support

Metrics
-------

* Add ``per_server.connection`` metrics (total, active, blocked connections)
  with configurable match rules and metric prefix
* Add ``proxy.process.cache.stripe.lock_contention`` and
  ``proxy.process.cache.writer.lock_contention`` metrics
* Add ``proxy.process.http.000_responses`` metric for responses where no
  valid status code was sent
* Add ``proxy.process.http.429_responses`` metric for rate-limiting
  monitoring
* ``proxy.process.http.incoming_requests`` now counted at transaction start
  to include all requests including early errors and redirects
* RAM cache stats updates: counters for all memory cache types and
  aggregation buffer hits

Logging
-------

* SnowflakeID: Add organizationally unique 64-bit identifiers for
  connections, with ``psfid`` log field
* Add ``chiv`` log field from real-ip plugin for verified client IP
* Add ``mstsms`` log field for all milestone timing as a single CSV field
* Add support for ``PP2_SUBTYPE_SSL_CIPHER`` and ``PP2_SUBTYPE_SSL_VERSION``
  proxy protocol fields in logging
* Add backtrace information to crash logs with 10-second collection timeout
* Fix ``msdms`` log fields to emit ``-`` instead of ``-1`` for unset
  milestones
* Fix ``UA_BEGIN_WRITE`` milestone to be set unconditionally
* Fix ``difference_msec()`` epoch leak when start milestone is unset
* Fix Transfer-Encoding:chunked log field preservation
* Fix log field type for ``cqpv`` and ``sqpv``
* Rename slow log field ``tls_handshake`` to ``ua_tls_handshake`` and add
  ``server_tls_handshake`` field

Configuration
-------------

* :ts:cv:`proxy.config.http.negative_caching_list` and
  :ts:cv:`proxy.config.http.negative_revalidating_list` are now overridable
  per-remap via ``conf_remap``
* Add retry connect with exponential backoff via
  ``proxy.config.http.connect_attempts_retry_backoff_base``
* Add IP address source setting for ACL with proxy protocol
* Add ``proxy.config.http.per_client.connection.exempt_list`` to exempt
  specific IP addresses from per-client connection limits
* Automatic caching of parsed STRING config values for overridable configs,
  improving performance when plugins call ``TSHttpTxnConfigStringSet()``

Tools
-----

* traffic_ctl: Add ``hostdb status`` command to dump HostDB records and
  health state, with hostname filtering
* traffic_ctl: Add ``config reset`` command to reset configuration records
  to defaults
* traffic_ctl: Add ``--append`` option for ``server debug`` to append debug
  tags instead of replacing them
* traffic_grapher: New real-time metrics visualization tool with multi-host
  comparison, keyboard navigation, and iTerm2 inline image support
* ArgParser: Add mutually exclusive option groups and option dependencies
* Migrate from Pipenv to uv for autest Python environment management

TS API
------

* Add ``TSHttpTxnVerifiedAddrSet/Get`` for verified client IP address
  management (used by the new real-ip plugin)
* Add ``TSHttpTxnNextHopStrategySet/Get`` and related APIs for Next Hop
  Strategy rebind during a transaction
* Add ``TSConnectionLimitExemptListSet/Add/Clear`` APIs for per-client
  connection exempt list management

Parent Selection
----------------

* Configurable hash algorithm (SipHash-2-4/SipHash-1-3), seeds, and
  replica count for consistent hash parent selection, available globally
  in ``records.yaml``, per-rule in ``parent.config``, and per-strategy in
  ``strategies.yaml``
* Add ``host_override`` in ``parent.config`` for SNI name handling when
  using another CDN as parent

HTTP Protocol
-------------

* Remap: Add ``http+unix`` scheme support for Unix Domain Socket matching
* Warn on shadow remap rules when an existing rule shadows an inserted one
* Return 400 on chunk parse errors
* Reject malformed Host header ports

Performance
-----------

* HuffmanCodec with LiteSpeed implementation for HTTP/2, addressing huffman
  decode performance hot spots
* Reduce ``ink_get_hrtime`` calls in the event loop with configurable update
  frequency
* Optimize ``ts::Random`` by reusing distribution objects (~7% improvement)
* remap_acl autest speedup via config reload (7 min to 2 min)
* Speed up day/month header parsing (~10x faster via integer packing)

Infrastructure
--------------

* Complete PCRE to PCRE2 migration across all plugins and core code
* USDT tracepoints: connection fd tracking (origin pool, session
  attachment, readiness polling), HTTP result codes in
  ``milestone_sm_finish``, cache directory insert/delete
* Catch2 updated to v3.9.1 with library model and FetchContent
* ATSReplayTest: new autest extension for writing tests via replay.yaml
  files

Notable Bug Fixes
-----------------

* Fix NetAcceptAction::cancel() use-after-free race condition between
  cancel and acceptEvent threads
* Fix DbgCtl use-after-free shutdown crash via leaky singleton pattern
* Fix DenseThreadId static destruction order fiasco causing crashes on
  CentOS
* Fix LoadedPlugins::remove crash during static destruction when
  EThreads are already gone
* Fix HttpSM::tunnel_handler crash on unhandled VC events
  (VC_EVENT_ACTIVE_TIMEOUT, VC_EVENT_ERROR, VC_EVENT_EOS)
* Fix possible crashes on OCSP request timeout from null pointer
  dereference
* Fix cache retry assertion on TSHttpTxnServerAddrSet when re-entering
  cache miss path
* Fix origins unintentionally marked as down when using server session
  reuse
* Fix negative_caching_lifetime being overridden by ttl-in-cache for
  negative responses
* Fix s-maxage not respected with Authorization headers per RFC 7234
* Fix malformed Cache-Control directives (semicolons instead of commas)
  now properly ignored per RFC 7234
* Fix 100 Continue with transform skip_bytes issue causing assertion
  failure when compress plugin is active
* Fix cache directory corruption in parallel dir sync where stripe index
  advanced during multi-step AIO writes
* Fix request buffering with post_copy_size=0 causing POST failures
* Fix 1xx race in build_response where 103 Early Hints tunnel completion
  overlapped with final response
* Fix HTTPHdr host cache invalidation when Host header is modified via
  MIME layer, preventing SNI warnings with garbage characters


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


