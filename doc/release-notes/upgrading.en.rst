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

Upgrading to ATS v9.x
======================

.. toctree::
   :maxdepth: 1

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
  instead of ``-1``. A configuration that explicitly set this to ``-1`` is no
  longer valid and must be updated or removed.

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

* A ``Transfer-Encoding`` whose final coding is not ``chunked`` is rejected.

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
  code parsing is bounded rather than allowed to overflow.

* Body preclusion for ``CONNECT`` now applies only to successful responses, so an
  error response to a ``CONNECT`` is framed by its own headers.

* HTTP/2 header decoding enforces a per-field size limit, rejects oversized HPACK
  indexes and rejects integers that overflow during XPACK decoding.
  ``CONTINUATION`` accumulation that would overflow the header block length is
  rejected.

* HTTP/2 ``Content-Length`` is compared against the payload length at full width,
  so a mismatch cannot be hidden by truncation.

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

* A PROXY header carrying no addresses is handled rather than assumed to carry
  them, including on TLS connections.

Access Control Corrections
~~~~~~~~~~~~~~~~~~~~~~~~~~

These rules now apply as written. Re-check any configuration that relies on them.

* Nonstandard method matching in remap ACL filtering applies as written.

* Loop detection is retained on outbound-transparent ports.

Connection Reuse and TLS
~~~~~~~~~~~~~~~~~~~~~~~~

* An inbound TLS session is bound to the SNI and ``verify_client`` policy that
  issued it, so a session offered outside that scope completes a full handshake
  instead of resuming. See `Inbound TLS Session Resumption`_ below. This
  supersedes the narrower check that rejected a resumed session presenting no
  client certificate where the matching :file:`sni.yaml` entry required one; that
  check is retained as a last line of defense and is normally unreachable.

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

* ``s3_auth`` strips client-supplied ``x-amz-`` request headers that fall outside
  the scope its signature covers, rather than forwarding headers the signature
  does not authenticate.

* ``magick`` requires a signing key and allowlists the ``convert`` operators it
  will run. An existing configuration without a signing key will not load.

* ``esi`` bounds nested ESI comments and request length, and validates
  ``esi:include`` URLs before fetching them.

* ``certifier`` rejects SNI names that are unsafe to use as a filename, rather
  than deriving a path from them.

* Several plugins now bound input they previously accepted without limit, so an
  input larger than the bound is refused instead of being processed:
  ``webp_transform`` bounds both the buffered body and the ImageMagick decode,
  ``xdebug`` caps the ``X-Remap`` header to the buffer content, and
  ``multiplexer`` rejects oversized chunks.

* ``compress`` stops the transform after a ``deflate()`` error rather than
  continuing, and ``prefetch`` constrains relative paths.

* ``regex_map`` in :file:`remap.config` and ``fqdn`` in :file:`sni.yaml` now
  require a regex to match the whole hostname, so a pattern matching only part of
  a subject can no longer satisfy a rule meant for something else
  (CVE-2026-22068). See :ref:`admin-regex-best-practices`.

Remapping
---------

One of the biggest changes in ATS v9.0.0 is that URL rewrites, as specified in a :file:`remap.config` rule, now always happens
**before** all plugins are executed. This can have significant impact on behavior, since plugins might now see a different URL than
they did in prior versions. In particular, plugins modifying the cache key could have serious problems (see the section below for
details).

YAML
----

We are moving configurations over to YAML, and thus far, the following configurations are now fully migrated over to YAML:

* :file:`logging.yaml` (*was* `logging.config` or `logging.lua`)
* :file:`ip_allow.yaml` (*was* `ip_allow.config`)

In addition, a new file for TLS handshake negotiation configuration is added:

* :file:`sni.yaml` (this was for a while named ssl_server_name.yaml in Github)

HTTP/2
------

HTTP/2 is enabled by default in ATS v9.0.0 on TLS ports. This can be customized per individual domain or globally via
:file:`sni.yaml`. Note that this is a major behavior change compared to prior releases where HTTP/2 was explicitly
required to be enabled per listener port via :ts:cv:`proxy.config.http.server_ports`.

Configuration Settings: records.config
--------------------------------------

These are the changes that are most likely to cause problems during an upgrade. Take special care making sure you have updated your
configurations accordingly.

Outbound Session Sharing and Client Certificates
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The default value of :ts:cv:`proxy.config.http.server_session_sharing.match` is unchanged at
``both``, but ``both`` now selects ``cert`` in addition to ``ip`` and ``host``.  A pooled
outbound TLS session is only reused for a transaction that would present the same client
certificate, because the origin authorizes the connection on the certificate sent during the
handshake and reuse performs no second handshake.

Deployments that override :ts:cv:`proxy.config.ssl.client.cert.filename` per remap rule or from
a plugin will see outbound sessions pool per certificate, and so more outbound handshakes.  Set
``ip,host`` explicitly to select what ``both`` selected before; that also turns the
client-certificate check on session reuse back off.

Inbound TLS Session Resumption
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Inbound TLS sessions are now bound to the SNI they were issued under and to that
SNI's ``verify_client`` policy from :file:`sni.yaml`, including the trust anchors
the client certificate was verified against. A session ticket or session ID
offered under a different SNI, after the policy for its SNI changed, or after
those trust anchors were replaced, is no longer resumed and completes a full
handshake instead. Replacing the bundle named by
:ts:cv:`proxy.config.ssl.CA.cert.filename` takes effect on the next
:option:`traffic_ctl config reload`, which is also when a replaced bundle begins
to apply to fresh handshakes. Clients that key their session caches by hostname,
which includes browsers and curl, are unaffected. Sessions issued by a previous
release may not resume after the upgrade and complete a full handshake instead.
See :ref:`admin-mtls` for details.

Connection Management
~~~~~~~~~~~~~~~~~~~~~

The old settings for origin connection management included the following settings:

* `proxy.config.http.origin_max_connections`
* `proxy.config.http.origin_max_connections_queue`
* `proxy.config.http.origin_min_keep_alive_connections`

These are all gone, and replaced with the following set of configurations:

* :ts:cv:`proxy.config.http.per_server.connection.max` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.match` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.alert_delay`
* :ts:cv:`proxy.config.http.per_server.connection.queue_size`
* :ts:cv:`proxy.config.http.per_server.connection.queue_delay`
* :ts:cv:`proxy.config.http.per_server.connection.min`


Renamed records.config Settings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
* `proxy.config.http.proxy_protocol_whitelist` was renamed to :ts:cv:`proxy.config.http.proxy_protocol_allowlist`
* `proxy.config.net.max_connections_active_in` was renamed to :ts:cv:`proxy.config.net.max_requests_in`

Removed records.config Settings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following settings are simply gone, and have no purpose:

* `proxy.config.config_dir` (see PROXY_CONFIG_CONFIG_DIR environment variable)
* `proxy.config.cache.storage_filename` (see next section as well)
* `proxy.config.http.server_tcp_init_cwnd` (see Solaris section below)
* `proxy.config.http.parent_proxy_routing_enable` (implicit by use of :file:`parent.config`)
* `proxy.config.disable_configuration_modification` (is always on)

All the `Vary` related configuration overrides were eliminated, prefer to set this via the origin server, or modify the `Vary`
header on server responses instead.

* `proxy.config.http.cache.vary_default_text`
* `proxy.config.http.cache.vary_default_images`
* `proxy.config.http.cache.vary_default_other`

SSLv3 support has been removed

* `proxy.config.ssl.client.SSLv3`

The option to disable the caching of an empty response (zero length body response) has been removed.  Zero length body responses are no
longer a special case and are handled the same as a response with a non-zero length body.

* `proxy.config.http.cache.allow_empty_doc`


* `proxy.config.ssl.client.verify.server` was deprecated for ATS 8.x and has been removed.  Use
   :ts:cv:`proxy.config.ssl.client.verify.server.properties` instead.

Deprecated records.config Settings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following configurations still exist, and functions, but are considered deprecated and will be removed in a future release. We
**strongly** encourage you to avoid using any of these:

  * :ts:cv:`proxy.config.socks.socks_config_file`
  * :ts:cv:`proxy.config.log.config.filename`
  * :ts:cv:`proxy.config.url_remap.filename`
  * :ts:cv:`proxy.config.ssl.server.multicert.filename`
  * :ts:cv:`proxy.config.ssl.servername.filename`
  * ``proxy.config.http.parent_proxy.file``
  * ``proxy.config.cache.control.filename``
  * ``proxy.config.cache.ip_allow.filename``
  * ``proxy.config.cache.hosting_filename``
  * ``proxy.config.cache.volume_filename``
  * ``proxy.config.dns.splitdns.filename``

Settings With New Defaults
~~~~~~~~~~~~~~~~~~~~~~~~~~

The following settings have changed from being `on` by default, to being off:

* :ts:cv:`proxy.config.ssl.TLSv1`
* :ts:cv:`proxy.config.ssl.TLSv1_1`
* :ts:cv:`proxy.config.ssl.client.TLSv1`
* :ts:cv:`proxy.config.ssl.client.TLSv1_1`

The default cipher list has changed:
* :ts:cv:`proxy.config.ssl.server.cipher_suite`

* :ts:cv:`proxy.config.exec_thread.autoconfig.scale` went from a value of 1.5 to 1.  This controls the number of worker threads and
  the ratio is now worker thread 1 to 1 processing thread on the CPU or CPUs.


Settings with new behavior
~~~~~~~~~~~~~~~~~~~~~~~~~~
* :ts:cv:`proxy.config.http.connect_attempts_max_retries_dead_server` specifies the exact number of times a dead server will be
  retried for each new request.  Before ATS tried 1 time more than this setting making it impossible to set the retry value to 0.

Metrics
-------

Renamed or Modified Metrics
~~~~~~~~~~~~~~~~~~~~~~~~~~~

`proxy.process.http2.current_client_sessions` is renamed to be :ts:stat:`proxy.process.http2.current_client_connections`

:ts:stat:`proxy.process.http.current_client_transactions` used to record both the number current of HTTP/1.1 and HTTP/2 requests.
Now it only records the number current of HTTP/1.1 client requests.  Please use
:ts:stat:`proxy.process.http2.current_client_streams` to get the number of current HTTP/2 client requests.

Removed Metrics
~~~~~~~~~~~~~~~

The following metrics have been removed.

* `proxy.process.ssl.ssl_error_want_read`
* `proxy.process.ssl.ssl_error_want_write`
* `proxy.process.ssl.ssl_error_want_x509_lookup`
* `proxy.process.ssl.ssl_error_zero_return`

Command Line Options
--------------------

The following command line options were either renamed or removed.

* `--with-max-api-stats` was replace with `--maxRecords` to specify the total number of metric instead of just the total API metrics
  to use when running ATS.
* `--read_core` was removed and gdb should be used instead.

Deprecated or Removed Features
------------------------------

The following features, configurations and plugins are either removed or deprecated in this version of ATS. Deprecated features
should be avoided, with the expectation that they will be removed in the next major release of ATS.

Removed the log collation feature along with its configuration settings (`proxy.local.log.collation*`).

Rollback Configuration
~~~~~~~~~~~~~~~~~~~~~~
The rollback configuration code was removed in for ATS v9.0.0.  This featured copied the configuration files to have version numbers
if the end user wanted to rollback to a previous version of the configuration.

`records.config.shadow` and `records.config.snap` support has also been removed in order to clean up and simply loading of
configuration files.

API Changes
-----------

Our APIs are guaranteed to be compatible within major versions, but we do make changes for each new major release.

Removed APIs
~~~~~~~~~~~~

* ``TSHttpTxnRedirectRequest``

Renamed or Modified APIs
~~~~~~~~~~~~~~~~~~~~~~~~

* ``TSVConnSSLConnectionGet`` is renamed to be :c:func:`TSVConnSslConnectionGet`
* :c:func:`TSHttpTxnServerPush` now returns a :c:type:`TSReturnCode`
* :c:func:`TSContSchedule` by default will run on the same thread from which they are called.
* :c:func:`TSFetchUrl` and :c:func:`TSFetchCreate` now return a :c:type:`TSFetchSM`
* ``TSHttpArgIndexReserve`` is now renamed to be :c:func:`TSHttpTxnArgIndexReserve`

Cache
-----

The cache in this releases of ATS is compatible with previous versions of ATS. You would not expect to lose your cache, or have to
reinitialize the cache when upgrading.

However, due to changes in how remap plugins are processed, your cache key *might* change. In versions to v9.0.0, the first plugin
in a remap rule would get the pristine URL, and subsequent plugins would get the remapped URL. As of v9.0.0, **all** plugins now
receive the remapped URL. If you are using a plugin that modifies the cache key, e.g. :ref:`admin-plugins-cachekey`, if it was
evaluated first in a remap rule, the behavior (input) changes, and therefore, cache keys can change!

Caches created with ATS v2.x are incompatible and can't be loading into ATS v9.0.0 or later. We feel that this is an unlikely
scenario, but if you do run into this, clearing the cache is required.

Plugins
-------

Promoted Plugins
~~~~~~~~~~~~~~~~

The following plugins have been promoted from experimental to stable plugins:
* `cache_range_requests`
* `certifier`
* `multiplexer`
* `prefetch`
* `remap_purge`

Removed Plugins
~~~~~~~~~~~~~~~

The following plugins have been removed because they are no longer used or maintained:
* `balancer`
* `buffer_uplaod`
* `header_normalize`
* `hipes`
* `memcached_remap`
* `stale_while_revalidate`
* `mysql_remap`

The following plugins have changes that might require you to change
configurations.

Header Rewrite
~~~~~~~~~~~~~~

* The `%{PATH}` directive is now removed, and instead you want to use `%{CLIENT-URL:PATH}`. This was done to unify the behavior of
  these operators, rather than having this one-off directive.

* `header-rewrite-expansion` was removed and replaced with `header-rewrite-concatenations`

Cache Key
~~~~~~~~~
* `--ua-blacklist` was renamed to `--ua-blocklist` and `--ua-whitelist` was renamed to `--ua-allowlist`

Logging
-------
* cqhv has been deprecated and cqpv should be used instead.

Library Dependencies and Builds
-------------------------------
* TCL is no longer required to build ATS.
* The minium OpenSSL version to build ATS is now 1.0.2.
* To build the documentation we now require Sphinx 2.0.1
* CentOS 6 support has been removed.
* Ubuntu 14.04 support has been removed.

Platform Specific
-----------------

Solaris is no longer a supported platform, but the code is still there. However, it's unlikely to work, and unless someone takes on
ownership of this Platform, it will be removed from the source in ATS v10.0.0. For more details, see issue #5553. With Solaris no
longer supported, we also eliminated `proxy.config.http.server_tcp_init_cwnd`, which has no purpose on all other platforms.
