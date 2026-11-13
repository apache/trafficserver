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

Upgrading to ATS v11.x
======================

TLS Session Resumption
----------------------

Inbound TLS sessions are now bound to the SNI they were issued under and to that
SNI's ``verify_client`` policy from :file:`sni.yaml`, including the trust anchors
the client certificate was verified against. A session ticket or session ID
offered under a different SNI, after the policy for its SNI changed, or after
those trust anchors were replaced, is no longer resumed and completes a full
handshake instead. Replacing the bundle named by
:ts:cv:`proxy.config.ssl.CA.cert.filename` takes effect on the next
:option:`traffic_ctl config reload`, which is also when a replaced bundle begins
to apply to fresh handshakes. Clients that key their session
caches by hostname, which includes browsers and curl, are unaffected. Sessions
issued by a previous release may not resume after the upgrade and complete a
full handshake instead. See :ref:`admin-mtls` for details.

API Changes
-----------

The handle returned by :cpp:func:`TSPortDescriptorParse` must now be released
with :cpp:func:`TSPortDescriptorDestroy`. The descriptor can be destroyed
immediately after :cpp:func:`TSPortDescriptorAccept` returns because the
listener does not retain it.

``ts::Metrics``, in the installed ``tsutil/Metrics.h``, no longer has an
iterator. ``Metrics::iterator``, ``begin()``, ``end()`` and ``find()`` are
removed, and enumeration is now ``Metrics::for_each(func)``, which invokes
``func(name, type, value)`` for each metric:

.. code-block:: cpp

    ts::Metrics::instance().for_each([](std::string_view name, ts::Metrics::MetricType type, int64_t value) {
      // ...
    });

Handing out a position let a caller name a slot the store was free to change
underneath them, which is what the iterator could not be made safe against.
Reaching a single metric by name is ``lookup()``.

``Metrics::Storage::createSpan()`` and ``Metrics::rename()`` are also removed.
Spans handed out unnamed slots that only ``rename()`` could name, and
``rename()`` mutated a name that the lock free readers hand out views of.

The overridable configuration key ``TS_CONFIG_HTTP_SLOW_LOG_THRESHOLD`` and the
Lua configuration key ``TS_LUA_CONFIG_HTTP_SLOW_LOG_THRESHOLD`` are gone. A
plugin naming the first no longer compiles, and a Lua script naming the second
fails when it runs.

``RegexMatchContext``, in the installed ``tsutil/Regex.h``, is removed. A match
limit is now passed to ``Regex::exec`` as a ``Regex::Options`` value, in which a
zero field means the shared default:

.. code-block:: cpp

    Regex::Options opts;
    opts.match_limit = 1750;
    int rc = regex.exec(subject, matches, flags, opts);

Matches with options now use the shared match configuration, including its
1 MiB JIT stack, rather than PCRE2's defaults. Plugins that call
``Regex::exec(subject, matches, flags)`` must be recompiled, because the symbol
that call resolved to is gone.

The next hop strategy API added in 10.2 now uses the opaque
:cpp:type:`TSStrategy` handle instead of ``void const *``, and two functions are
renamed. The 10.2 functions are deprecated but still provided, so plugins built
against 10.2 keep working; they will be removed in a future major release:

* :cpp:func:`TSHttpTxnNextHopNamedStrategyGet` is replaced by :cpp:func:`TSHttpTxnNextHopStrategyFind`.
* :cpp:func:`TSHttpNextHopStrategyNameGet` is replaced by :cpp:func:`TSNextHopStrategyNameGet`.
* :cpp:func:`TSHttpTxnNextHopStrategySet` takes a :cpp:type:`TSStrategy`; the
  ``void const *`` overload is deprecated. Clear the strategy with ``nullptr``,
  since ``NULL`` or ``0`` is ambiguous between the overloads.
* :cpp:func:`TSHttpTxnNextHopStrategyGet` returns a :cpp:type:`TSStrategy`,
  which still converts implicitly to ``void const *``.

:cpp:func:`TSHttpTxnNextHopStrategySet` now normally rejects a handle that is
not a strategy of the transaction's configuration, such as one cached across a
configuration reload; the call is logged and has no effect. The check compares
addresses, so plugins must still re-obtain handles after a reload. Remap plugins can
resolve strategies once during ``TSRemapNewInstance`` with the new
:cpp:func:`TSRemapNextHopStrategyFind`, :cpp:func:`TSRemapNextHopStrategyGet` and
:cpp:func:`TSRemapNextHopStrategySet`.

traffic_ctl Changes
-------------------

:program:`traffic_ctl` grades a server error by the severity of its annotations, see
:option:`traffic_ctl --error-level`. An annotation without a severity counts as an error, so with
the default level, ``error``, the exit status only changes in these cases:

* ``traffic_ctl server drain`` on a server that is already draining, and ``traffic_ctl server drain
  --undo`` on one that is not, report a warning and exit ``0``. They used to exit ``2``. Pass
  ``--error-level=warn`` to keep treating them as failures.
* With ``--format json``, a server error sets the exit status, as in text output. Most commands used
  to exit ``0`` in that case.
* ``traffic_ctl server debug enable --append`` sets nothing when the request that reads the current
  tags fails, and exits ``2``, or ``75`` when the error is below the level. It used to set the new
  tags on their own, replacing the current ones.

Annotations that carry a severity are printed with it, for example ``- [3000] Warn: Server already
draining.``, and a JSONRPC error ``data`` entry carries a ``severity`` field when its handler sets
one, see :ref:`jsonrpc-node-errors-severity`.

A ``--`` now protects the token after it at every level of the command line, so a value spelled
like a global option reaches the command that asked for it: ``traffic_ctl config set NAME -- -f``
sets the record to ``-f`` instead of failing on ``--format``.

Security Hardening
------------------

A series of security fixes changed defaults and tightened validation. Many of
them refuse traffic, or reject configuration, that earlier releases accepted, so
review this section before upgrading. Where a fix has a published CVE identifier
it is noted inline; refer to the security advisory for this release for the full
set of identifiers, severities and affected version ranges.

This section covers only changes with operator-visible effect. The same work
included a larger number of memory-safety and bounds fixes -- use-after-free,
out-of-bounds access, integer overflow and stack-allocation fixes across the
core, the protocol stacks and the plugins -- which change no behavior a
configuration can observe and are not listed individually.

Changed Defaults
~~~~~~~~~~~~~~~~

* :file:`ip_allow.yaml` now ships outbound rules that deny ``CONNECT`` tunnels to
  unspecified, loopback, private, link-local and IPv4-mapped IPv6 destination
  addresses. If a deployment intentionally proxies ``CONNECT`` to one of those
  ranges, add a more specific ``apply: out`` allow rule *before* the deny rule:
  the first matching range determines the outcome.

* :ts:cv:`proxy.config.http2.default_buffer_water_mark` now defaults to ``32768``
  instead of ``-1``, and its accepted range is now ``1024`` to ``4294967295``. A
  configuration that explicitly set this to ``-1``, or to any value below
  ``1024``, is no longer valid and must be updated or removed.

* The ``tsmemcache`` plugin now binds only to the loopback address by default. It
  previously bound the wildcard address, which exposed the |TS| cache to every
  host able to reach the port. To bind a specific address, pass it as the second
  plugin argument, and read the security notes in
  :doc:`../admin-guide/plugins/memcache.en` first.

* Two settings that were previously accepted unchecked are now range checked, so a
  configuration carrying an out-of-range value fails to load rather than being
  silently clamped or overflowing. Their defaults are unchanged.
  :ts:cv:`proxy.config.http.header_field_max_size` accepts ``0`` to ``65535``, and
  :ts:cv:`proxy.config.hostdb.round_robin_max_count` accepts ``1`` to ``1024``.

Stricter Request and Response Validation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each of the following refuses a message earlier releases would have processed.
The common theme is framing that can be read two ways.

* A ``HEAD`` request carrying ``Content-Length`` or ``Transfer-Encoding`` is
  rejected. Set :ts:cv:`proxy.config.http.reject_head_with_content` to ``0`` to
  restore the previous behavior.

* A ``Transfer-Encoding`` whose final coding is not ``chunked`` is rejected, as is
  any ``Transfer-Encoding`` appearing in HTTP/2 or HTTP/3 request headers, where
  the field has no meaning.

* Chunked parsing is stricter in two further ways: a bare ``LF`` terminating a
  chunked trailer is rejected under strict parsing, and a chunk extension
  quoted-string value containing ``CR`` or ``LF`` is rejected.

* A header field longer than the 16-bit field-length limit is rejected rather
  than silently truncated to its low 16 bits. A ``Host`` header longer than its
  buffer is likewise bounded rather than overrunning it (CVE-2026-33930).

* A response that cannot be parsed is refused whatever status it claims. Earlier
  releases relayed a ``302`` carrying a ``Location`` header even when parsing its
  header block failed; that exemption keyed only off the status code and never
  accounted for why the parse failed. The responses a broken origin actually
  depends on resolve as a completed parse and are unaffected.

* A URL whose port is outside the 16-bit range is rejected, and response status
  code parsing is bounded rather than allowed to overflow. An IPv6 literal with an
  out-of-range hextet is rejected where it was previously accepted, which can
  affect a configuration file carrying a malformed address.

* Body preclusion for ``CONNECT`` now applies only to successful responses, so an
  error response to a ``CONNECT`` is framed by its own headers.

* HTTP/2 and HTTP/3 header decoding enforces a per-field size limit, rejects
  oversized HPACK indexes and rejects integers that overflow during XPACK
  decoding. ``CONTINUATION`` accumulation that would overflow the header block
  length is rejected. Outstanding HTTP/2 ``SETTINGS`` frames are bounded, and an
  unnecessary ``SETTINGS`` frame closes the connection.

* HTTP/2 ``Content-Length`` is compared against the payload length at full width,
  so a mismatch cannot be hidden by truncation.

* HTTP/2 origin trailers are no longer forwarded to HTTP/1 clients, which cannot
  frame them.

Headers Now Removed
~~~~~~~~~~~~~~~~~~~

* Client-supplied headers whose name begins with ``@`` are stripped before any
  plugin hook runs, in both directions. ``@`` headers are |TS| internal, and
  accepting one from a client let a request set state a plugin would read as
  |TS|'s own. A plugin that relied on reading a client-supplied ``@`` header will
  no longer see it. The log line reporting a strip is throttled, so a client
  sending them in volume does not flood the diagnostics (CVE-2026-33267).

* Headers named in the ``Connection`` field are removed per RFC 9110 section
  7.6.1, rather than being forwarded to the next hop.

Proxy Protocol
~~~~~~~~~~~~~~

* A PROXY v1 header whose port is out of range is rejected.

* The Proxy Protocol allowlist is consulted only once the header preface has been
  seen, so an allowlisted peer that sends no PROXY header is treated as though it
  sent none rather than inheriting the allowlisted path.

* A PROXY header carrying no addresses is handled rather than assumed to carry
  them, including on TLS connections.

Access Control Corrections
~~~~~~~~~~~~~~~~~~~~~~~~~~

These rules now apply as written. Re-check any configuration that relies on them.

* Nonstandard method matching in remap ACL filtering applies as written.

* The client SNI action test in ``SNI_IpAllow`` applies as written.

* Loop detection is retained on outbound-transparent ports.

Connection Reuse and TLS
~~~~~~~~~~~~~~~~~~~~~~~~

* An inbound TLS session is bound to the SNI and ``verify_client`` policy that
  issued it, so a session offered outside that scope completes a full handshake
  instead of resuming. See `TLS Session Resumption`_ above. This supersedes the
  narrower check that rejected a resumed session presenting no client certificate
  where the matching :file:`sni.yaml` entry required one; that check is retained
  as a last line of defense and is normally unreachable.

* Reusing a pooled outbound session requires that its client certificate match
  the one the current transaction would present, so a transaction cannot inherit
  another's authenticated identity. HTTP/2 origin certificates are likewise
  verified on session reuse.

* A server session is not returned to the pool while unread bytes remain on it,
  so a later request cannot be answered from another transaction's leftover
  response data. Related to this, an origin connection owed an unsent request
  body is closed rather than pooled, including under ``Expect: 100-continue``
  (CVE-2025-31699), and the unread body of an ``Expect: 100-continue`` ``POST``
  is no longer forwarded as a new request (CVE-2025-55702).

* The outbound ``server_name`` SNI policy is validated before use, and the bytes
  it names are owned across a configuration reload rather than referenced into a
  freed configuration.

Origin Selection
~~~~~~~~~~~~~~~~

* ``parent.config`` retries are capped at one per window, so a parent that is
  failing cannot be retried repeatedly within the same window.

Plugins
~~~~~~~

* ``origin_server_auth`` strips client-supplied ``x-amz-`` request headers that
  fall outside the scope its signature covers, rather than forwarding headers the
  signature does not authenticate.

* ``magick`` requires a signing key and allowlists the ``convert`` operators it
  will run. An existing configuration without a signing key will not load.

* ``esi`` bounds nested ESI comments and request length, and validates
  ``esi:include`` URLs before fetching them.

* ``certifier`` rejects SNI names that are unsafe to use as a filename, rather
  than deriving a path from them.

* Several plugins now bound input they previously accepted without limit, so an
  input larger than the bound is refused instead of being processed:
  ``webp_transform`` bounds both the buffered body and the ImageMagick decode,
  ``stale_response`` caps its buffering, ``xdebug`` caps the ``X-Remap`` header to
  the buffer content, ``multiplexer`` rejects oversized chunks, and ``url_sig``
  requires the delimiter in its ``C=`` parameter.

* ``combo_handler`` rejects an empty ``Content-Type`` when an allowlist is
  configured; ``compress`` stops the transform after a ``deflate()`` error rather
  than continuing; ``prefetch`` constrains relative paths; ``rate_limit`` no
  longer allows the queue to be bypassed.

* Regex rules that drive a security or routing decision can be required to match
  the whole subject. This is off by default. See
  :ts:cv:`proxy.config.regex.anchor_policy_rules` and
  :ref:`admin-regex-best-practices` for the call sites it covers, the config-load
  report that names each pattern it would change, and the rewrites that preserve
  behavior. ``regex_map`` in :file:`remap.config` and ``fqdn`` in
  :file:`sni.yaml` already require a whole-subject match and are unaffected by
  the setting (CVE-2026-22068).

Configuration Changes
---------------------
:file:`cache.yaml` is now the default file selected by
``proxy.config.cache.control.filename``. Existing deployments that provide
only :file:`cache.config` must either retain the legacy file by setting that
record to ``cache.config`` or convert the file:

.. code-block:: bash

   traffic_ctl config convert cache cache.config cache.yaml

Review overlapping rules before deploying a conversion. Legacy
:file:`cache.config` combines actions from every matching rule, while
:file:`cache.yaml` applies only the first matching rule.

The following :file:`records.yaml` changes have been made:

- The slow log feature has been removed, and the following settings are removed:

  * ``proxy.config.http.slow.log.threshold``
  * ``proxy.config.http2.connection.slow.log.threshold``
  * ``proxy.config.http2.stream.slow.log.threshold``

  Every 10.x default :file:`records.yaml` sets ``proxy.config.http.slow.log.threshold: 0``,
  so most existing installations still carry it. With these settings removed,
  :program:`traffic_server` starts on such a file and prints an
  ``Ignoring field ... Not registered`` warning, but
  ``traffic_server -C verify_config`` exits ``2`` until the configuration block
  is deleted.

  To log slow requests instead, configure a filter in :file:`logging.yaml` on
  ``%<{TS_MILESTONE_SM_FINISH-TS_MILESTONE_SM_START}msdms>`` plus milestone
  fields, for example:

  .. code-block:: yaml

     logging:
       filters:
         - name: slowlog
           action: accept
           condition: '%<{TS_MILESTONE_SM_FINISH-TS_MILESTONE_SM_START}msdms> GTE 100'

       formats:
         - name: "slowlog"
           format: '[%<cruuid>] client_ip: %<chi>:%<chp> protocol: %<cqpv> url: %<cquc> status: %<pssc> X-ID: %<{X-Id}cqh> bytes=%<pscl> client state: %<cfsc> server state: %<sssc> tls_handshake: %<{TS_MILESTONE_TLS_HANDSHAKE_END-TS_MILESTONE_TLS_HANDSHAKE_START}msdms> ua_begin: %<{TS_MILESTONE_UA_BEGIN-TS_MILESTONE_SM_START}msdms> ua_first_read: %<{TS_MILESTONE_UA_FIRST_READ-TS_MILESTONE_SM_START}msdms> ua_read_header_done: %<{TS_MILESTONE_UA_READ_HEADER_DONE-TS_MILESTONE_SM_START}msdms> cache_open_read_begin: %<{TS_MILESTONE_CACHE_OPEN_READ_BEGIN-TS_MILESTONE_SM_START}msdms> cache_open_read_end: %<{TS_MILESTONE_CACHE_OPEN_READ_END-TS_MILESTONE_SM_START}msdms> cache_open_write_begin: %<{TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN-TS_MILESTONE_SM_START}msdms> cache_open_write_end: %<{TS_MILESTONE_CACHE_OPEN_WRITE_END-TS_MILESTONE_SM_START}msdms> dns_lookup_begin: %<{TS_MILESTONE_DNS_LOOKUP_BEGIN-TS_MILESTONE_SM_START}msdms> dns_lookup_end: %<{TS_MILESTONE_DNS_LOOKUP_END-TS_MILESTONE_SM_START}msdms> server_connect: %<{TS_MILESTONE_SERVER_CONNECT-TS_MILESTONE_SM_START}msdms> server_connect_end: %<{TS_MILESTONE_SERVER_CONNECT_END-TS_MILESTONE_SM_START}msdms> server_first_read: %<{TS_MILESTONE_SERVER_FIRST_READ-TS_MILESTONE_SM_START}msdms> server_read_header_done: %<{TS_MILESTONE_SERVER_READ_HEADER_DONE-TS_MILESTONE_SM_START}msdms> server_close: %<{TS_MILESTONE_SERVER_CLOSE-TS_MILESTONE_SM_START}msdms> ua_write: %<{TS_MILESTONE_UA_BEGIN_WRITE-TS_MILESTONE_SM_START}msdms> ua_close: %<{TS_MILESTONE_UA_CLOSE-TS_MILESTONE_SM_START}msdms> sm_finish: %<{TS_MILESTONE_SM_FINISH-TS_MILESTONE_SM_START}msdms> plugin_active: %<{TS_MILESTONE_PLUGIN_ACTIVE-TS_MILESTONE_SM_START}msdms> plugin_total: %<{TS_MILESTONE_PLUGIN_TOTAL-TS_MILESTONE_SM_START}msdms>'

       logs:
         - filename: slowlog
           format: slowlog
           filters:
             - slowlog

- :ts:cv:`proxy.config.http2.max_ping_frames_per_minute` now applies only to inbound connections.
  Outbound HTTP/2 connections use the new
  :ts:cv:`proxy.config.http2.max_ping_frames_per_minute_out`, which defaults to ``300``. Origin
  connections now allow 300 PING frames a minute by default where they allowed 60, so a default
  configuration closes fewer origin connections. If you set
  :ts:cv:`proxy.config.http2.max_ping_frames_per_minute` to anything other than the default,
  origin connections no longer use that value and get ``300`` instead. That is stricter for
  origins if you had set it negative (no limit) or above ``300``; set
  :ts:cv:`proxy.config.http2.max_ping_frames_per_minute_out` as well to keep your limit for them.

- The addresses in :ts:cv:`proxy.config.http.per_client.connection.exempt_list` are now also
  exempt from :ts:cv:`proxy.config.net.connections_throttle`. If you already use this list for the
  per-client limit, those addresses can now also connect when ATS is at the connection limit, and
  they can take ATS past that limit. Review the list before you upgrade.

:ts:cv:`proxy.config.net.inactivity_check_frequency` no longer trades load for
timeout accuracy, and if you raised it you are now paying the accuracy for
nothing.

Connection timeouts used to be found by walking every open connection on a
thread once per check, so the cost of a check grew with the number of
connections open and checking less often genuinely reduced it — which is what
the documentation for this setting previously advised. Timeouts are now held in
a per-thread timer wheel, so a check costs what the deadlines that have come
due cost, independent of how many connections are open.

The setting still controls timeout resolution: a timeout fires on the first
check at or after its deadline. So a value raised to relieve load now only
delays timeouts by up to that interval. If you tuned it for that reason,
returning it to the default of 1 costs nothing and restores accuracy.

Plugins
-------

The :ref:`admin-plugins-stats-over-http` plugin prints gauge values as signed
integers in its JSON, CSV and Prometheus output. Earlier versions printed a
negative gauge as an unsigned value, for example ``18446744073709551574`` for
``-42``, or ``9223372036854775767`` with ``--wrap-counters``. The
``--wrap-counters`` option now applies only to counters.

Two changes affect the Prometheus version 0.0.4 output of the plugin:

* When two metric names map to the same Prometheus name, the plugin writes
  them as one family: one ``# HELP`` and ``# TYPE`` block, then all of their
  samples. Earlier versions wrote a separate block for each metric.
* The plugin writes sample lines that are longer than 255 bytes. Earlier
  versions left out such a sample, but wrote its ``# HELP`` and ``# TYPE``
  lines.

The plugin renders the statistics on a task thread. Each request waits for a
render that starts after the request arrives. The ``--max-age-ms`` option lets
a render also answer the requests that arrive up to that many milliseconds
after it starts; the default is ``0`` for the global plugin and ``1000`` for a
remap rule. When requests wait and no render finishes for ten seconds, the
requests get a ``503`` response. The ``--wait-timeout-ms`` option sets this
time.

The plugin creates metrics of its own, such as
``plugin.stats_over_http.requests`` and ``plugin.stats_over_http.renders``, so
its output has more metrics than before, in each format.

ClientHello Extension Types
---------------------------

On OpenSSL builds, ``TSClientHello::get_extension_types()`` now lists every
extension type in the ClientHello, including GREASE values and types OpenSSL
does not recognize, matching its behavior on BoringSSL. Previously it omitted
those extensions on OpenSSL. As a result, ClientHello fingerprints computed
from this list on OpenSSL, such as the JA3 and JA4 values from
``jax_fingerprint`` and ``ja4_fingerprint``, change for clients that send
extensions OpenSSL does not recognize, for example Chrome's ALPS extension.
GREASE values alone do not change these fingerprints, since they are dropped
when the fingerprints are computed. ``ja3_fingerprint`` reads OpenSSL's list
directly, so its output does not change, and on OpenSSL its JA3 can now differ
from the JA3 that ``jax_fingerprint`` logs for the same client.

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
  * Removed the built-in stats and cache inspector pages that were previously accessible via the |TS| HTTP interface

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
- The records.yaml entry ``proxy.config.http.connect_attempts_max_retries_down_server`` is now deprecated in favor of
  :ts:cv:`proxy.config.http.connect_attempts_max_retries_suspect_server`. The new name aligns with the
  ``HostDBInfo::State::SUSPECT`` state it actually applies to (a recovering origin allowed a limited probe budget after
  :ts:cv:`proxy.config.http.down_server.cache_time` elapses). When only the deprecated record is set, its value is mirrored
  forward to the new record and a warning is logged. When both are set, the new record wins.
- The records.yaml entry ``proxy.config.exec_thread.watchdog.timeout_ms`` is now deprecated in favor of
  :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms`. When only the deprecated record is set, the watchdog
  behaves as before - it only logs a warning for each event loop iteration that runs past the timeout - and a
  deprecation warning is logged. The new watchdog metrics and backpressure settings require
  :ts:cv:`proxy.config.exec_thread.watchdog.timeout_shrink_ms`. When both are set, the new record wins.
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
- The records.yaml entry :ts:cv:`proxy.config.http.proxy_protocol_allowlist` is now enforced
  only for connections on Proxy Protocol-enabled ports that begin with a Proxy Protocol
  header preface. Non-Proxy Protocol traffic on flexible Proxy Protocol ports is no longer
  restricted by this setting; use :file:`ip_allow.yaml` for general source-IP access control.
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
- The records.yaml entry ``proxy.config.ssl.TLSv1_3`` has been renamed to :ts:cv:`proxy.config.ssl.TLSv1_3.enabled`.
- The records.yaml entry ``proxy.config.ssl.client.TLSv1_3`` has been renamed to :ts:cv:`proxy.config.ssl.client.TLSv1_3.enabled`.
- The records.yaml entry :ts:cv:`proxy.config.allocator.iobuf_chunk_sizes` has been added
  to enable more control of iobuffer allocation.
- The records.yaml entry :ts:cv:`proxy.config.allocator.hugepages` will enable
  allocating iobuffers and cache volumes from hugepages if configured in the
  system.
- The records.yaml entry ``proxy.config.plugin.compiler_path`` has been added to specify an optional compiler tool path for compiling plugins.
- The default value of :ts:cv:`proxy.config.http.server_session_sharing.match` is unchanged at
  ``both``, but ``both`` now selects ``cert`` in addition to ``ip`` and ``host``.  A pooled
  outbound TLS session is only reused for a transaction that would present the same client
  certificate, because the origin authorizes the connection on the certificate sent during the
  handshake and reuse performs no second handshake.  Deployments that override
  :ts:cv:`proxy.config.ssl.client.cert.filename` per remap rule or from a plugin will see
  outbound sessions pool per certificate, and so more outbound handshakes.  Set ``ip,host``
  explicitly to select what ``both`` selected before.

The ``ssl_multicert.config`` file has been replaced with
:file:`ssl_multicert.yaml`. The new file uses YAML format with a top-level
``ssl_multicert`` key containing a sequence of certificate entries. Use
``traffic_ctl config convert ssl_multicert`` to convert existing
configuration files:

.. code-block:: bash

    traffic_ctl config convert ssl_multicert ssl_multicert.config ssl_multicert.yaml

See :file:`ssl_multicert.yaml` for the new format documentation.

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

Deprecated Plugins
~~~~~~~~~~~~~~~~~~
The following plugins have been deprecated.

  * healthchecks - please use the statichit plugin instead
  * icap

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
- The cache metrics ``proxy.process.cache.direntries.used`` and
  ``proxy.process.cache.volume_X.direntries.used`` now keep counting stale
  directory entries until they are reclaimed, at the latest by the next cache
  cycle wrap, so they read higher through each cycle and drop at the wrap
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
