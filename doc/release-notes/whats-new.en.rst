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

What's New in ATS v9.x
=======================

This version of ATS includes over <x> commits, from <y> pull requests. A total of <z> contributors have participated in this
development cycle.

.. toctree::
   :maxdepth: 1

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
