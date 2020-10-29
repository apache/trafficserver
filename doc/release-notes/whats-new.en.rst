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

* Experimental QUIC support (draft 27 and 29).
* TLS v1.3 0RTT support.

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

Parent Selection
~~~~~~~~~~~~~~~~

A new directive, `ignore_self_detect`, is added to the :file:`parent.config` format. This allows you to parent proxy to
sibling proxies, without creating loops.


HTTP/2 settings

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


Plugins
-------

xdebug
~~~~~~

* A new directive, `fwd=<n>` to control the number of hops the header is forwarded for.

Plugin APIs
-----------

The API for server push is promoted to stable, and modified to return an error code, to be consistent with other similar APIs. The
new prototype is:

.. code-block:: c

    TSReturnCode TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len);

A new set of APIs for scheduling continuations on a specific set of threads has been added:

.. code-block:: c

    TSAction TSContScheduleOnPool(TSCont contp, TSHRTime timeout, TSThreadPool tp);
    TSAction TSContScheduleOnThread(TSCont contp, TSHRTime timeout, TSEventThread ethread);


There is a new API for redoing a cache lookup, typically after a URL change, or cache key update:

.. code-block:: c

    TSReturnCode TSHttpTxnRedoCacheLookup(TSHttpTxn txnp, const char *url, int length)

New APIs for TLS client context retrievals were added:

.. code-block:: c

    TSReturnCode TSSslClientContextsNamesGet(int n, const char **result, int *actual);
    TSSslContext TSSslClientContextFindByName(const char *ca_paths, const char *ck_paths);
