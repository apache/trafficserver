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

What's New in ATS v9.x?
=======================

ATS v9.x is the current major release train, with many new features and improvements. This document describes  new
features and changes in these releases. Currently three minor versions have been released:

* :ref:`ATS v9.0.x<version_9_0_x>` (last version was ``v9.0.2``)
* ATS v9.1.x (last version was ``v9.1.4``)
* :ref:`ATS v9.2.x<version_9_2_x>` (current stable version ``v9.2.0``)

Keep in mind that only the latest (current) minor version is actively maintained, which is `v9.2.x`. This
is expected to the the last minor relase in this train, with an upcoming `v10.0.0` release.

.. toctree::
   :maxdepth: 1

.. _version_9_0_x:

Version 9.0.x Updates
=====================

New Features
------------

This version of ATS has a number of new features (details below), but we're particularly excited about the following features:

* Experimental QUIC support (draft 27 and 29)
* TLS v1.3 0RTT support
* Significant HTTP/2 performance improvement
* PROXY protocol support
* Significant improvments to parent selection, including a new configuration file :file:`strategies.yaml`
* Several new plugins

A new infrastructure and tool chain for end-to-end testing and replaying traffic is introduced, the Proxy Verifier.

PROXY protocol
~~~~~~~~~~~~~~

ATS now supports the `PROXY <https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt>`_ protocol, on the inbound side. The
incoming PROXY data gets transformed into the ``Forwarded`` header.

Developer features
~~~~~~~~~~~~~~~~~~

* Add support for dtrace style markers (SDT) and include a few markers at locations of interest to users of SystemTap, dtrace, and
  gdb. See :ref:`developer-debug-builds`.

Command line tools
~~~~~~~~~~~~~~~~~~

* The :program:`traffic_server` program now has two new maintenance commands: ``verify_global_plugin`` and ``verify_remap_plugin``.
  These commands load a plugin's shared object file and verify it meets minimal global or remap plugin API requirements.

New configurations
------------------

Some new configurations were added, on existing features.

HTTP/2
~~~~~~

The following new configurations are available to rate limit some potentially abusive clients.

* :ts:cv:`proxy.config.http2.max_settings_per_frame`
* :ts:cv:`proxy.config.http2.max_settings_per_minute`

Overall, the performance for HTTP/2 is significantly better in ATS v9.x.

Parent Selection
~~~~~~~~~~~~~~~~

A new directive, `ignore_self_detect`, is added to the :file:`parent.config` format. This allows you to parent proxy to
sibling proxies, without creating loops. The setting for `proxy.config.http.parent_proxy_routing_enable`
is no longer needed, it's implicit by the usage of the :file:`parent.config` configuration file.

A new option was added to :file:`parent.config` for which status codes triggers a simple retry,
`simple_server_retry_responses`.

A configuration file for the new parent selection strategy is added, :file:`strategies.yaml`.

SNI
~~~

A new option to control how host header and SNI name mismatches are handled has been added. With this new
setting, :ts:cv:`proxy.config.http.host_sni_policy`, when set to `0`, no checking is performed. If this
setting is `1` or `2` (the default), the host header and SNI values are compared if the host header value
would have triggered a SNI policy.

A new blind tunneling option was added, `partial_blind_route`, which is similar to the existing
`forward_route` feature.

The captured group from a FQDN matching in :file:`sni.yaml` can now be used in the `tunnel_route`, as e.g.
`$1` and `$2`.

Plugin reload
~~~~~~~~~~~~~

A new setting was added to turn off the dynamic plugin reload, :ts:cv:`proxy.config.plugin.dynamic_reload_mode`.
By default, the feature is still enabled.

Updated host matching settings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The options for host matching configuration, ts:cv:`proxy.config.http.server_session_sharing.match` has been
augmented significantly, the full list of host matches now are:

   ============= ===================================================================
   Value         Description
   ============= ===================================================================
   ``none``      Do not match and do not re-use server sessions.
   ``ip``        Re-use server sessions, checking only that the IP address and port
                 of the origin server matches.
   ``host``      Re-use server sessions, checking that the fully qualified
                 domain name matches. In addition, if the session uses TLS, it also
                 checks that the current transaction's host header value matchs the session's SNI.
   ``both``      Equivalent to ``host,ip``.
   ``hostonly``  Check that the fully qualified domain name matches.
   ``sni``       Check that the SNI of the session matches the SNI that would be used to
                 create a new session.  Only applicable for TLS sessions.
   ``cert``      Check that the certificate file name used for the server session matches the
                 certificate file name that would be used for the new server session.  Only
                 applicable for TLS sessions.
   ============= ===================================================================

RAM cache per volumes
~~~~~~~~~~~~~~~~~~~~~

In :file:`volume.config`, you can now specify if a volume should have a RAM cache or not. This
can be particularly useful when a volume is assigned to a RAM disk already. The new option flag
is `ramcache`, with a value of `true` or `false`.

Incompatible records.config settings
------------------------------------

These are the changes that are most likely to cause problems during an upgrade.
Take special care making sure you have updated your configurations accordingly.

Connection management
~~~~~~~~~~~~~~~~~~~~~

The old settings for origin connection management included the following settings:

* ``proxy.config.http.origin_max_connections``
* ``proxy.config.http.origin_max_connections_queue``
* ``proxy.config.http.origin_min_keep_alive_connections``

These are all gone, and replaced with the following set of configurations:

* :ts:cv:`proxy.config.http.per_server.connection.max` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.match` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.alert_delay`
* :ts:cv:`proxy.config.http.per_server.connection.queue_size`
* :ts:cv:`proxy.config.http.per_server.connection.queue_delay`
* :ts:cv:`proxy.config.http.per_server.connection.min`


Logging and Metrics
-------------------

In addition to logging indivdiual headers, we now have comparable log tags that dumbs
the entire header. This table shows the additions, and what they correspond with.

    ============== ===================
    Original Field All Headers Variant
    ============== ===================
    cqh            cqah
    pqh            pqah
    psh            psah
    ssh            ssah
    cssh           cssah
    ============== ===================


A new log field for the elliptic curve was introduced, `cqssu`. Also related to TLS, the
new log field `cssn` allows logging the SNI server name from the client handshake.

New origin metrics
~~~~~~~~~~~~~~~~~~

A large number of metrics were added for the various cases where ATS closes an origin
connection. This includes:

* :ts:stat:`proxy.process.http.origin_shutdown.pool_lock_contention`
* :ts:stat:`proxy.process.http.origin_shutdown.migration_failure`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_server`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_server_no_keep_alive`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_server_eos`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_server_plugin_tunnel`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_transform_read`
* :ts:stat:`proxy.process.http.origin_shutdown.release_no_sharing`
* :ts:stat:`proxy.process.http.origin_shutdown.release_no_keep_alive`
* :ts:stat:`proxy.process.http.origin_shutdown.release_invalid_response`
* :ts:stat:`proxy.process.http.origin_shutdown.release_invalid_request`
* :ts:stat:`proxy.process.http.origin_shutdown.release_modified`
* :ts:stat:`proxy.process.http.origin_shutdown.release_misc`
* :ts:stat:`proxy.process.http.origin_shutdown.cleanup_entry`
* :ts:stat:`proxy.process.http.origin_shutdown.tunnel_abort`

Metric scaling
~~~~~~~~~~~~~~

In previous versions of ATS, you had to rebuild the software to increase the maximum
number of metrics that the system could handle. This is replaced with a new command
line option for :program:`traffic_manager`, `--maxRecords`. The old build configure
option for this, `--with-max-api-stats`, is also eliminated.

TLS version metrics
~~~~~~~~~~~~~~~~~~~

A set of new metrics for SSL and TLS versions have been added:

* proxy.process.ssl.ssl_total_sslv3
* proxy.process.ssl.ssl_total_tlsv1
* proxy.process.ssl.ssl_total_tlsv11
* proxy.process.ssl.ssl_total_tlsv12
* proxy.process.ssl.ssl_total_tlsv13

Plugins
-------

statichit
~~~~~~~~~

This is a new, generic static file serving plugin. This can replace other plugins as well,
for example the :program:`healthchecks`.

maxmind
~~~~~~~

A new geo-ACL plugin was merged, :program:`maxmind`. This should support the newer APIs
from MaxMind, and should likely replace the old plugin in future release of ATS.

memory_profile
~~~~~~~~~~~~~~

This is a helper plugin for debugging and analyzing memory usage with e.g. JEMalloc.

slice
~~~~~

The :program:`slice` plugin has had a major overhaul, and has a significant number of new features
and configurations. If you use the slice plugin, we recommend you take a look at the
documentation again.

xdebug
~~~~~~

* A new directive, `fwd=<n>` to control the number of hops the header is forwarded for.

cert_reporting_tool
~~~~~~~~~~~~~~~~~~~

This is a new plugin to log examine and log information on loaded certificates.

Plugin APIs
-----------

The API for server push is promoted to stable, and modified to return an error code, to be consistent with other similar APIs. The
new prototype is:

.. code-block:: c

    TSReturnCode TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len);

A new set of APIs for scheduling continuations on a specific set of threads has been added:

.. code-block:: c

    TSAction TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp)
    TSAction TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread)


There is a new API for redoing a cache lookup, typically after a URL change, or cache key update:

.. code-block:: c

    TSReturnCode TSHttpTxnRedoCacheLookup(TSHttpTxn txnp, const char *url, int length)

New APIs for TLS client context retrievals were added:

.. code-block:: c

    TSReturnCode TSSslClientContextsNamesGet(int n, const char **result, int *actual)
    TSSslContext TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths)

In addition to these, a new set of APIs were added for the effective TLS handshake with
the client. These APIs also have equivalent Lua APIs:

.. code-block:: c

    int TSVConnIsSslReused(TSVConn sslp)
    const char *TSVConnSslCipherGet(TSVConn sslp)
    const char *TSVConnSslProtocolGet(TSVConn sslp)
    const char *TSVConnSslCurveGet(TSVConn sslp)

We have also added two new alert mechanisms for plugins:

.. code-block:: c

    void TSEmergency(const char *fmt, ...) TS_PRINTFLIKE(1, 2)
    void TSFatal(const char *fmt, ...) TS_PRINTFLIKE(1, 2)

A new API was added to allow control over whether the plugin will participate in the
dynamic plugin reload mechanism:

.. code-block:: c

    TSReturnCode TSPluginDSOReloadEnable(int enabled)

User Arg Slots
~~~~~~~~~~~~~~

The concept of user argument slots for plugins was completely redesigned. The old behavior
still exists, but we now have a much cleaner, and smaller, set of APIs. In addition, it also
adds a new slot type, `global`, which allows a slot for plugins to retain global data across
reloads and cross-plugins.

The new set of APIs has the following signature:

.. code-block:: c

    typedef enum {
       TS_USER_ARGS_TXN,   ///< Transaction based.
       TS_USER_ARGS_SSN,   ///< Session based
       TS_USER_ARGS_VCONN, ///< VConnection based
       TS_USER_ARGS_GLB,   ///< Global based
       TS_USER_ARGS_COUNT  ///< Fake enum, # of valid entries.
     } TSUserArgType;


    TSReturnCode TSUserArgIndexReserve(TSUserArgType type, const char *name, const char *description, int *arg_idx);
    TSReturnCode TSUserArgIndexNameLookup(TSUserArgType type, const char *name, int *arg_idx, const char **description);
    TSReturnCode TSUserArgIndexLookup(TSUserArgType type, int arg_idx, const char **name, const char **description);
    void TSUserArgSet(void *data, int arg_idx, void *arg);
    void *TSUserArgGet(void *data, int arg_idx);

One fundamental change here is that the opaque `data` parameter to `TSUserArgSet` and `TSUserArgGet` are context aware.
If you pass in a `Txn` pointer, it behaves as a transaction user arg slot. If you pass in a nullptr, it becomes the new
`global` slot behavior (since there is no context). The valid contexts are:

   ============== =======================================================================
   data type      Semantics
   ============== =======================================================================
   ``TSHttpTxn``  The implicit context is for a transaction (``TS_USER_ARGS_TXN``)
   ``TSHttpSsn``  The implicit context is for a transaction (``TS_USER_ARGS_SSN``)
   ``TSVConn``    The implicit context is for a transaction (``TS_USER_ARGS_VCONN``)
   ``nullptr``    The implicit context is global (``TS_USER_ARGS_GLB``)
   ============== =======================================================================

.. _version_9_2_x:

Version 9.2.x Updates
=====================

New Features
------------

In addition to the new features from v9.0.x, this release includes:

* A number of performance improvements have been made, and this should show noticeable gains.
* Log throttling can now be controlled via two new settings.
* A new feature to create and manage pre-warmed TLS tunnels was added.
* The setting for :ts:cv:`proxy.config.ssl.client.sni_policy` can now be controlled via :file:`sni.yaml`.
* New features, and overall improvements for Parent Selection were added.
* There is now an internal inspector to produce stats for remap rule frequency.
* BoringSSL is now properly supported, and should be a drop in replacement for OpenSSL.
* A new allocator, `mimalloc`, is now available via the ``--with-mimalloc`` configure option.
* A large number of autest have been added, and improvements made to the existing tests.

A new infrastructure and tool chain for end-to-end testing and replaying traffic is introduced, the Proxy Verifier.

Log throttling
~~~~~~~~~~~~~~

The throttling of logging is controlled by the new configurations:

* :ts:cv:`proxy.config.log.proxy.config.log.throttling_interval_msec`
* :ts:cv:`proxy.config.diags.debug.throttling_interval_msec`

This feature is useful to control logging pressure under traffic spikes, bad
configurations or DDoS attacks.

Parent Selection Improvements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* A setting to control the ring mode was added, ``ring_mode``, with
  three possible settings: `exhaust_ring`, `alternate_ring`, or `peering_ring`.
* Two new settings are now available to control how parents are marked down. The
  configurations are :ts:cv:`proxy.config.http.parent_proxy.enable_parent_timeout_markdowns`
  and :ts:cv:`proxy.config.http.parent_proxy.disable_parent_markdowns`.

Remap inspector
~~~~~~~~~~~~~~~

This feature can help identifying which remap rule is most frequently used. This helps
when ordering remap rules, and also when removing unused rules. This is not well documented
unfortunately, for some details see https://github.com/apache/trafficserver/pull/7936 .

HTTP/2
------

* New metrics were added for streams status, :ts:stat:`proxy.process.http2.max_concurrent_streams_exceeded_in`
  and :ts:stat:`proxy.process.http2.max_concurrent_streams_exceeded_out`.
* The new setting :ts:cv:`proxy.config.http2.stream_error_sampling_threshold` allows for
  control over the sampling rate of stream errors.

New configuration options
-------------------------

* The :ts:cv:`proxy.config.cache.log.alternate.eviction` configuration
  allows control over logging of alternate eviction events. This is useful to get
  a better idea of how many alternates are being evicted, and to tune the setting.
* Better control over loop detection was added, via the
  :ts:cv:`proxy.config.http.max_proxy_cycles` configuration.
* An option to control tunnel activity check frequencey was added,
  :ts:cv:`proxy.config.tunnel.activity_check_period`. In addition, a new metric was added as well,
  :ts:stat:`proxy.process.tunnel.current_active_connections`.
* For debugging, a new option allowing to log TLS session keys was added, the new
  :ts:cv:`proxy.config.ssl.keylog_file` setting specifies the log file to write to.
* The setting :ts:cv:`proxy.config.net.sock_notsent_lowat` was added, allowing for better control
  over the TCP send buffer. This can both reduce memory usage and reduce buffer bloat.

Misc changes
------------

* The default for :ts:cv:`proxy.config.ssl.origin_session_cache` is now `1` (enabled).
* A new log tag is available for custom log formats, ``pqssr``. This is an indicator if
  TLS session reuse to origin was succesful.
* Two new metrics around loop detection was added, :ts:stat:`proxy.process.http.http_proxy_loop_detected`
  and :ts:stat:`proxy.process.http.http_proxy_mh_loop_detected`.
* A metric :ts:stat:`proxy.process.hostdb.total_serve_stale` now tracks HostDB lookups that
  served stale data.
* A new setting :ts:cv:`proxy.config.track_config_files` allows for control over tracking
  configuration files for changes. This should be used with care.

Plugins
-------

The following plugins have new features or configurations.

header_rewrite
~~~~~~~~~~~~~~

* A new directive ``rm-destination`` was added to remove either the query parameters or the PATH
  from the incoming request.
* A new ``%{CACHE}`` condition was added, exposing the cache lookup status on the request.
* The new directive ``set-http-cntl`` allows control over various HTTP transaction features, such
  as turning off logging, cacheability etc.
* The ``set-body`` directive lets you override the body factory, and force a body on
  requests that are considered errors early on (before going to cache or origin).
* As a special matcher ('{}'), the inbound IP addresses can be matched against a list of IP ranges, e.g.
  ``cond %{INBOUND:REMOTE-ADDR} {192.168.201.0/24,10.0.0.0/8}``.

generator
~~~~~~~~~

POST request are now handled as well as GET requests.

cache_promote
~~~~~~~~~~~~~

* We can now count bytes served as the threshold within the LRU.
* A new setting, ``--internal-enabled``, was added to allow promotion on internal request.

xdebug
~~~~~~

A new header ``X-Effective-URL``, to expose the effective (remapped) URL.

regex_revalidate
~~~~~~~~~~~~~~~~

New metrics for misses and stale counts were added.

url_sig
~~~~~~~

Add a new directive, ``url_type = pristine``, to use the pristine URL for signing.

prefetch
~~~~~~~~

A new option was added, ``--fetch-query``. This allows giving hints for subsequent
URLs via a query parameter.

authproxy
~~~~~~~~~

A new option to enable the cache for internal requests was added, ``--cache-internal``.

slice
~~~~~

The plugin now supports prefetching future ranges, similar to to the ``prefetch`` plugin.

cache_range_requests
~~~~~~~~~~~~~~~~~~~~

* The setting ``--verify-cacheability`` now gives better control over what is cacheable. This
  is useful together with other plugins such as `cache_promote`.
* A new setting ``--cache-complete-responses`` now allows for "200 OK" responses to be cached
  as well.

rate_limit
~~~~~~~~~~

* Two new options were added for metrics, ``--prefix`` and ``--tag``.
* An IP reputation system was added to the plugin.

static_hit
~~~~~~~~~~

The plugin can now serve content out of a directory.

otel_tracer
~~~~~~~~~~~

This is a new plugin for OpenTelemetry tracing.

http_stats
~~~~~~~~~~

This new plugin provides stats over HTTP, but is implemented as a remap plugin. This should
make the older ``stats_over_http`` obsolete.

Plugin APIs
-----------

The transction control APIs where refactored and promoted to that ``ts.h`` public APIs. This adds
:c:func:`TSHttpTxnCntlGet` and :c:func:`TSHttpTxnCntlSet`, and the c:enum::`TSHttpCntlType` enum.

For introspection into the SNI, the new :c:func:`TSVConnSslSniGet` was added.
